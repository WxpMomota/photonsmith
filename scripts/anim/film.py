#!/usr/bin/env python3
"""Look at a cartoon film from the command line: its stills, a contact sheet, the whole video.

The films themselves are C++ (src/shared/cartoon/: cartoon_lib.h is the toolkit, one header per film, cartoons.h lists them), so there is one implementation and the GUI,
`--video-preset` and this script all render the same thing. This script only drives ray_tracer:

  python scripts/anim/film.py list                              the films (ray_tracer --list-video-presets)
  python scripts/anim/film.py still fireflies@13.5              one frame, to look at (cartoon:<key>@<seconds>; no @ is the film's own best still)
  python scripts/anim/film.py sheet fireflies --count 16        the whole film as a contact sheet: 16 stills, evenly spaced, tiled with their times
  python scripts/anim/film.py sheet little-robot --times 2,8,20,30
  python scripts/anim/film.py video fireflies                   the finished video (a film is a video preset: --video-preset <key>)
  python scripts/anim/film.py doctor tengwang                   every shot at its start, middle and end, small, with a report of what looks wrong (black, very dark, blown-out
                                                                and flat frames, jumps in brightness across a cut, slow frames; and, for a film that names its subjects, a subject out of the
                                                                frame, behind the camera or hidden behind something, and a camera against a wall) and the whole film's render time; exit 1
                                                                on a warning. --no-framing skips the subject checks (they add a depth pass to each frame)

Options (before or after the command): --exe PATH (default: RayTracer_Package/ray_tracer next to this repository), --cpu (default is the GPU, denoised), --height N (picture height,
default 270 for a sheet and 720 otherwise), --spp N, --out PATH. Plain Python 3; the contact sheet needs Pillow (pip install pillow).
"""
import argparse
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def find_exe(explicit):
    if explicit:
        return explicit
    for name in ("ray_tracer.exe", "ray_tracer"):
        path = os.path.join(REPO, "RayTracer_Package", name)
        if os.path.exists(path):
            return path
    sys.exit("ray_tracer not found next to this repository (RayTracer_Package/); pass --exe PATH")


def run_dir():
    """The program runs from its package folder (it finds the OptiX programs and the scene folders from there), whichever ray_tracer --exe names."""
    package = os.path.join(REPO, "RayTracer_Package")
    return package if os.path.isdir(package) else None


def renderer_flags(args):
    return ["--cpu"] if args.cpu else ["--gpu", "--denoise"]


def render_still(args, spec, out_png, height, spp):
    """One frame: ray_tracer ... cartoon:<key>[@t], the picture's width following from the film's 16:9."""
    width = round(height * 16 / 9)
    cmd = [find_exe(args.exe)] + renderer_flags(args) + ["--output", out_png, "--height", str(height), str(width), str(spp), "8", "cartoon:" + spec]
    done = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=run_dir())
    if done.returncode != 0 or not os.path.exists(out_png):
        sys.exit("render failed (%d):\n%s" % (done.returncode, done.stdout[-1500:]))


def film_length(args, key):
    """The film's length in seconds, from --list-video-presets ("V10  fireflies  cartoon  624 frames at 24 fps (26.0 s)  Fireflies (cartoon)")."""
    out = subprocess.run([find_exe(args.exe), "--list-video-presets"], stdout=subprocess.PIPE, text=True, cwd=run_dir()).stdout
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1] == key:
            for field in fields:
                if field.startswith("(") and field[1:].replace(".", "", 1).isdigit():
                    return float(field[1:])
    sys.exit("unknown film %r: python scripts/anim/film.py list" % key)


def cmd_list(args):
    sys.stdout.write(subprocess.run([find_exe(args.exe), "--list-video-presets"], stdout=subprocess.PIPE, text=True, cwd=run_dir()).stdout)


def cmd_still(args):
    out = args.out or os.path.join(tempfile.gettempdir(), "film_still_%s.png" % args.target.replace("@", "_at_"))
    render_still(args, args.target, out, args.height or 720, args.spp or 256)
    print(out)


def cmd_sheet(args):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        sys.exit("the contact sheet needs Pillow: pip install pillow")
    length = film_length(args, args.target)
    if args.times:
        times = [float(t) for t in args.times.split(",")]
    else:
        n = args.count or 16
        times = [length * (i + 0.5) / n for i in range(n)]
    height = args.height or 270
    tmp = tempfile.mkdtemp(prefix="film_sheet_")
    frames = []
    for i, t in enumerate(times):
        png = os.path.join(tmp, "f%03d.png" % i)
        render_still(args, "%s@%.3f" % (args.target, min(max(t, 0.0), length)), png, height, args.spp or 64)
        frames.append((t, Image.open(png).convert("RGB")))
        print("t=%.1f s" % t)
    w, h = frames[0][1].size
    cols = 4
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols, h * rows))
    draw = ImageDraw.Draw(sheet)
    for i, (t, im) in enumerate(frames):
        x, y = w * (i % cols), h * (i // cols)
        sheet.paste(im, (x, y))
        draw.text((x + 6, y + 6), "t=%.1f" % t, fill=(255, 255, 0))
    out = args.out or os.path.join(tmp, "sheet.png")
    sheet.save(out)
    print(out)


def film_info(args, key):
    """What `ray_tracer --film-shots <key>` says of a film: its facts and its shots [(start, name)]."""
    done = subprocess.run([find_exe(args.exe), "--film-shots", key], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=run_dir())
    if done.returncode != 0:
        sys.exit("unknown film %r: python scripts/anim/film.py list\n%s" % (key, done.stdout.strip()))
    info, shots = None, []
    for line in done.stdout.splitlines():
        f = line.split("\t")
        if f[0] == "film":
            info = {"key": f[1], "name": f[2], "duration": float(f[3]), "fps": int(f[4]), "width": int(f[5]), "height": int(f[6]), "samples": int(f[7]),
                    "seconds_per_frame": float(f[8])}
        elif f[0] == "shot":
            shots.append((float(f[1]), f[2] if len(f) > 2 else ""))
    if not info or not shots:
        sys.exit("ray_tracer --film-shots %s said nothing useful:\n%s" % (key, done.stdout))
    return info, shots


# What the doctor flags. Thresholds are on the picture as written (sRGB, 0..1): a check against the eye's judgement of a frame, not a measurement of light.
DARK = 0.06        # mean brightness below this: very dark (a light that is boxed in, an exposure far too low)
BLACK = 0.012      # mean below this and almost nothing brighter: black
CLIPPED = 0.08     # more than this share of pixels near white (brightness 0.95 or more): blown out (a sun in the haze, an exposure far too high). Brightness, not any one
                   # channel at full: a saturated red curtain is not blown out
FLAT = 0.025       # brightness this even over the whole frame (standard deviation): a camera inside something, or against a wall
JUMP = 5.0         # brightness ratio across a cut worth a note
SLOW = 3.0         # a frame this many times the film's median render time: worth a note
# The framing checks (a film's subjects, Film::subjectsAt, against the depth pass of the frame: `--aovs`, depth.Z = the distance along each camera ray, 0 where nothing is hit)
HIDDEN_RATIO = 0.75   # a subject is hidden when everything round its pixel is nearer than this share of its distance, and nearer by HIDDEN_MARGIN or more: a subject
HIDDEN_MARGIN = 0.3   # point is often inside what it names (a chest inside a body, a ship's middle inside its hull, a machine's centre inside its rings), not behind a wall
NEAR = 0.2            # depth below this ...
NEAR_SHARE = 0.5      # ... over this share of the frame: the camera against something (or inside it)


def frame_stats(png):
    """(mean, standard deviation, share of clipped pixels, share of near-black pixels) of a picture's brightness."""
    import numpy as np
    from PIL import Image
    a = np.asarray(Image.open(png).convert("RGB"), dtype=np.float32) / 255.0
    lum = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
    return float(lum.mean()), float(lum.std()), float((lum >= 0.95).mean()), float((lum < 0.02).mean())


def judge(stats):
    """The doctor's findings for one frame: [(level, text)], level "warn" or "note"."""
    mean, std, clipped, dark = stats
    found = []
    if mean < BLACK and dark > 0.97:
        found.append(("black", "black"))
    elif mean < DARK:
        found.append(("warn", "very dark (mean %.3f): a light boxed in, or the exposure far too low?" % mean))
    if clipped > CLIPPED:
        found.append(("warn", "blown out: %.0f%% of the frame near white" % (100 * clipped)))
    if std < FLAT and mean >= BLACK:
        found.append(("warn", "flat frame (brightness varies by %.3f): the camera inside something, or against a wall?" % std))
    return found


def film_subjects(args, key, t, width, height):
    """Where the film's subjects fall in a width x height frame at film time t (`ray_tracer --film-subjects`): [{"x", "y", "distance", "in_frame", "in_front"}], empty
    for a film that names none."""
    done = subprocess.run([find_exe(args.exe), "--film-subjects", key, "%.3f" % t, str(width), str(height)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          cwd=run_dir())
    found = []
    for line in done.stdout.splitlines():
        f = line.split("\t")
        if f[0] == "subject" and len(f) >= 7:
            found.append({"x": float(f[2]), "y": float(f[3]), "distance": float(f[4]), "in_frame": f[5] == "1", "in_front": f[6] == "1"})
    return found


def judge_framing(subjects, aovs_path):
    """The framing findings for one frame: [(level, text)]. A subject out of the frame or behind the camera (level "outside": the doctor decides, from the rest of the
    shot, whether it is entering or leaving); one hidden behind something nearer (the depth pass round its pixel); the camera against something (most of the frame very
    near)."""
    found = []
    depth = cover = None
    if aovs_path and os.path.exists(aovs_path):
        sys.path.insert(0, os.path.join(REPO, "scripts"))
        from exr_io import read_exr
        _, _, ch = read_exr(aovs_path)
        if "depth.Z" in ch:
            depth = ch["depth.Z"]
            cover = (ch["A"] > 0.5) & (depth > 0.0) if "A" in ch else depth > 0.0
    for i, s in enumerate(subjects):
        name = "the subject" if len(subjects) == 1 else "subject %d" % (i + 1)
        if not s["in_front"]:
            found.append(("outside", "%s is behind the camera" % name))
            continue
        if not s["in_frame"]:
            found.append(("outside", "%s is out of the frame (at %.0f, %.0f)" % (name, s["x"], s["y"])))
            continue
        if depth is None:
            continue
        h, w = depth.shape
        x, y = min(max(int(s["x"]), 0), w - 1), min(max(int(s["y"]), 0), h - 1)
        window = (slice(max(0, y - 1), y + 2), slice(max(0, x - 1), x + 2))
        nearest = min(HIDDEN_RATIO * s["distance"], s["distance"] - HIDDEN_MARGIN)
        if cover[window].all() and float(depth[window].max()) < nearest:
            found.append(("warn", "%s is hidden: something %.2f away is in front of it (it is %.2f away)" % (name, float(depth[window].max()), s["distance"])))
    if depth is not None and cover.any():
        near = float(((depth < NEAR) & cover).mean())
        if near > NEAR_SHARE:
            found.append(("warn", "the camera is against something: %.0f%% of the frame is nearer than %.2f" % (100 * near, NEAR)))
    return found


def cmd_doctor(args):
    """Every shot of a film at its start, middle and end, small, with a report of what looks wrong: black or very dark frames, blown-out ones, flat ones (a camera inside
    something), big jumps in brightness across a cut, and frames much slower to render than the rest - and an estimate of the whole film's render time."""
    try:
        from PIL import Image, ImageDraw
        import numpy  # noqa: F401  (frame_stats)
    except ImportError:
        sys.exit("the doctor needs Pillow and numpy: pip install pillow numpy")
    import concurrent.futures
    import time
    info, shots = film_info(args, args.target)
    duration = info["duration"]
    height = args.height or 120
    aspect = info["width"] / info["height"] if info["width"] > 0 and info["height"] > 0 else 16 / 9
    width = max(16, round(height * aspect))
    spp = args.spp or 8
    out_dir = args.out or os.path.join(tempfile.gettempdir(), "film_doctor_" + info["key"])
    os.makedirs(out_dir, exist_ok=True)
    # the moments: each shot's start, middle and end (a little inside its cuts, so that a frame is not the neighbour's)
    jobs = []
    only = {int(n) - 1 for n in args.shots.split(",")} if args.shots else None
    for i, (start, name) in enumerate(shots):
        if only is not None and i not in only:
            continue
        end = shots[i + 1][0] if i + 1 < len(shots) else duration
        inset = min(0.3, (end - start) / 4)
        for which, t in (("start", start + inset), ("middle", (start + end) / 2), ("end", end - inset)):
            jobs.append({"shot": i, "name": name, "which": which, "t": min(max(t, 0.0), duration * 0.9999)})

    # the framing checks: for a film that names its subjects (Film::subjectsAt), each frame also gets a depth pass
    framing = not args.no_framing and any(film_subjects(args, info["key"], j["t"], width, height) for j in jobs[::5])

    def render(job):
        png = os.path.join(out_dir, "s%02d_%s.png" % (job["shot"], job["which"]))
        cmd = [find_exe(args.exe)] + renderer_flags(args) + (["--aovs"] if framing else []) + ["--output", png, "--height", str(height), str(width), str(spp), "8",
                                                                                                "cartoon:%s@%.3f" % (info["key"], job["t"])]
        t0 = time.time()
        # the frame's time stops where the depth pass starts (that pass is the doctor's, not the film's)
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=run_dir())
        out, seconds = [], None
        for line in proc.stdout:
            out.append(line)
            if seconds is None and line.startswith("Writing the render passes"):
                seconds = time.time() - t0
        proc.wait()
        job["seconds"] = seconds if seconds is not None else time.time() - t0
        job["png"] = png if proc.returncode == 0 and os.path.exists(png) else None
        job["error"] = None if job["png"] else "".join(out)[-600:]
        job["aovs"] = os.path.splitext(png)[0] + ".aovs.exr" if framing else None
        if job["png"] and framing:
            job["subjects"] = film_subjects(args, info["key"], job["t"], width, height)
        return job

    print("%s: %d shots, %d frames at %d x %d, %d samples%s..." % (info["name"], len(shots), len(jobs), width, height, spp,
                                                                   ", with the framing checks" if framing else ""), flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs or 3) as pool:
        for k, job in enumerate(pool.map(render, jobs)):
            if k % 10 == 9:
                print("  %d of %d" % (k + 1, len(jobs)), flush=True)
    findings = []   # (level, shot, which, t, text)
    for job in jobs:
        if not job["png"]:
            said = (job["error"] or "").strip().splitlines()
            findings.append(("warn", job["shot"], job["which"], job["t"], "did not render" + (": " + said[-1] if said else "")))
            continue
        job["stats"] = frame_stats(job["png"])
        verdict = judge(job["stats"])
        for level, text in verdict:
            findings.append((level, job["shot"], job["which"], job["t"], text))
        if job.get("subjects") is not None and not any(level == "black" for level, _ in verdict):
            for level, text in judge_framing(job["subjects"], job["aovs"]):
                findings.append((level, job["shot"], job["which"], job["t"], text))
    # A black frame is on purpose when its whole shot is black (a cut to black, a title card) or it is the shot's start or end with a picture in its middle (a fade): a note.
    # A black middle in a shot that is not black throughout is a warning.
    def is_black(shot, which):
        j = next((j for j in jobs if j["shot"] == shot and j["which"] == which and j.get("stats")), None)
        return j is not None and any(level == "black" for level, _ in judge(j["stats"]))
    def black_finding(f):
        level, s, which, t, _ = f
        if all(is_black(s, w) for w in ("start", "middle", "end")):
            return ("note", s, which, t, "black (the whole shot: a cut to black or a title)")
        if which != "middle" and not is_black(s, "middle"):
            return ("note", s, which, t, "black at the shot's %s: a fade" % which)
        return ("warn", s, which, t, "black in the middle of a shot that is not black throughout")
    findings = [black_finding(f) if f[0] == "black" else f for f in findings]
    # A subject out of the frame at a shot's start or end, but in it in the middle, is entering or leaving: a note. Out of it in the middle is a warning.
    outside = {(f[1], f[2]) for f in findings if f[0] == "outside"}
    seen_middle = {j["shot"] for j in jobs if j["which"] == "middle" and j.get("subjects")}
    def outside_finding(f):
        level, s, which, t, text = f
        if which != "middle" and s in seen_middle and (s, "middle") not in outside:
            return ("note", s, which, t, text + (": it enters the frame" if which == "start" else ": it leaves the frame"))
        return ("warn", s, which, t, text)
    findings = [outside_finding(f) if f[0] == "outside" else f for f in findings]
    # across each cut: from the end of one shot to the start of the next
    by_key = {(j["shot"], j["which"]): j for j in jobs if j.get("stats")}
    for s in range(len(shots) - 1):
        a, b = by_key.get((s, "end")), by_key.get((s + 1, "start"))
        if a and b and min(a["stats"][0], b["stats"][0]) >= BLACK:
            ratio = max(a["stats"][0], b["stats"][0]) / min(a["stats"][0], b["stats"][0])
            if ratio > JUMP:
                findings.append(("note", s + 1, "start", b["t"], "brightness jumps %.0fx across the cut from shot %d" % (ratio, s + 1)))
    times = sorted(j["seconds"] for j in jobs if j["png"])
    median = times[len(times) // 2] if times else 0.0
    for j in jobs:
        if j["png"] and median > 0 and j["seconds"] > SLOW * median:
            findings.append(("note", j["shot"], j["which"], j["t"], "slow: %.1f s against a median of %.1f s" % (j["seconds"], median)))
    # the whole film's render time: the measured cost a frame (cartoons.h) shared out by how each shot rendered here against the median
    estimate = None
    if info["seconds_per_frame"] > 0 and median > 0:
        total = 0.0
        for s, (start, _) in enumerate(shots):
            end = shots[s + 1][0] if s + 1 < len(shots) else duration
            cost = [j["seconds"] for j in jobs if j["shot"] == s and j["png"]]
            relative = (sum(cost) / len(cost)) / median if cost else 1.0
            total += (end - start) * info["fps"] * info["seconds_per_frame"] * relative
        estimate = total
    # the sheet: a row a shot (start, middle, end), the flagged frames framed in red (warn) or yellow (note)
    tw = max(160, width)
    th = round(tw / aspect)
    label_h = 14
    rows = sorted({j["shot"] for j in jobs})
    sheet = Image.new("RGB", (tw * 3, (th + label_h) * len(rows)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    flagged = {}
    for level, s, which, t, text in findings:
        if level != "note" or flagged.get((s, which)) != "warn":
            flagged[(s, which)] = level if flagged.get((s, which)) != "warn" else "warn"
    for j in jobs:
        x, y = tw * ("start", "middle", "end").index(j["which"]), (th + label_h) * rows.index(j["shot"])
        if j["png"]:
            sheet.paste(Image.open(j["png"]).convert("RGB").resize((tw, th)), (x, y + label_h))
        level = flagged.get((j["shot"], j["which"]))
        if level:
            draw.rectangle([x, y + label_h, x + tw - 1, y + label_h + th - 1], outline=(255, 60, 60) if level == "warn" else (255, 220, 60), width=3)
        draw.text((x + 4, y + 1), "%d %s  %.1f s  (%.1f s)" % (j["shot"] + 1, j["name"][:28], j["t"], j["seconds"]), fill=(230, 230, 230))
    sheet_path = os.path.join(out_dir, "sheet.png")
    sheet.save(sheet_path)
    # the report
    lines = ["%s (%s): %d shots, %.0f s at %d fps" % (info["name"], info["key"], len(shots), duration, info["fps"])]
    if estimate:
        lines.append("Estimated render time at the film's own settings: %.1f hours (%.1f s a frame measured, shared out by how each shot rendered here)" % (estimate / 3600, info["seconds_per_frame"]))
    warns = [f for f in findings if f[0] == "warn"]
    notes = [f for f in findings if f[0] == "note"]
    lines.append("%d warning(s), %d note(s)" % (len(warns), len(notes)))
    for level, s, which, t, text in sorted(findings, key=lambda f: (f[1], ("start", "middle", "end").index(f[2]))):
        lines.append("  %-4s shot %2d %-26s %-6s %7.2f s  %s" % ("WARN" if level == "warn" else "note", s + 1, shots[s][1][:26], which, t, text))
    lines.append("Sheet: " + sheet_path)
    report = "\n".join(lines)
    with open(os.path.join(out_dir, "report.txt"), "w", encoding="utf-8") as f:
        f.write(report + "\n")
    print(report)
    sys.exit(1 if warns else 0)


def cmd_video(args):
    height = args.height or 720
    width = round(height * 16 / 9)
    cmd = [find_exe(args.exe)] + renderer_flags(args) + ["--video-preset", args.target, "--height", str(height), str(width), str(args.spp or 128), "8"]
    if args.out:
        cmd[1:1] = ["--output", args.out]
    sys.exit(subprocess.run(cmd, cwd=run_dir()).returncode)


def main():
    p = argparse.ArgumentParser(description="Look at a cartoon film (the films are C++: src/shared/cartoon/).")
    p.add_argument("command", choices=["list", "still", "sheet", "video", "doctor"])
    p.add_argument("target", nargs="?", help="still: <key>[@seconds]; sheet and video: <key>")
    p.add_argument("--exe")
    p.add_argument("--cpu", action="store_true")
    p.add_argument("--height", type=int)
    p.add_argument("--spp", type=int)
    p.add_argument("--out")
    p.add_argument("--count", type=int, help="sheet: how many stills, evenly spaced over the film (default 16)")
    p.add_argument("--times", help="sheet: comma-separated seconds instead of --count")
    p.add_argument("--jobs", type=int, help="doctor: how many frames to render at once (default 3)")
    p.add_argument("--no-framing", action="store_true", help="doctor: skip the subject checks (out of frame, hidden, camera against something)")
    p.add_argument("--shots", help="doctor: only these shots (comma-separated numbers, 1 the first: as the sheet counts)")
    args = p.parse_args()
    if args.out:
        args.out = os.path.abspath(args.out)   # (the program runs from its package folder)
    if args.exe:
        args.exe = os.path.abspath(args.exe)
    if args.command != "list" and not args.target:
        p.error("%s needs a film key" % args.command)
    {"list": cmd_list, "still": cmd_still, "sheet": cmd_sheet, "video": cmd_video, "doctor": cmd_doctor}[args.command](args)


if __name__ == "__main__":
    main()
