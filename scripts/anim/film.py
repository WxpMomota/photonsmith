#!/usr/bin/env python3
"""Look at a cartoon film from the command line: its stills, a contact sheet, the whole video.

The films themselves are C++ (src/shared/cartoon/: cartoon_lib.h is the toolkit, one header per film, cartoons.h lists them), so there is one implementation and the GUI,
`--video-preset` and this script all render the same thing. This script only drives ray_tracer:

  python scripts/anim/film.py list                              the films (ray_tracer --list-video-presets)
  python scripts/anim/film.py still fireflies@13.5              one frame, to look at (cartoon:<key>@<seconds>; no @ is the film's own best still)
  python scripts/anim/film.py sheet fireflies --count 16        the whole film as a contact sheet: 16 stills, evenly spaced, tiled with their times
  python scripts/anim/film.py sheet little-robot --times 2,8,20,30
  python scripts/anim/film.py video fireflies                   the finished video (a film is a video preset: --video-preset <key>)

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


def renderer_flags(args):
    return ["--cpu"] if args.cpu else ["--gpu", "--denoise"]


def render_still(args, spec, out_png, height, spp):
    """One frame: ray_tracer ... cartoon:<key>[@t], the picture's width following from the film's 16:9."""
    width = round(height * 16 / 9)
    cmd = [find_exe(args.exe)] + renderer_flags(args) + ["--output", out_png, "--height", str(height), str(width), str(spp), "8", "cartoon:" + spec]
    done = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if done.returncode != 0 or not os.path.exists(out_png):
        sys.exit("render failed (%d):\n%s" % (done.returncode, done.stdout[-1500:]))


def film_length(args, key):
    """The film's length in seconds, from --list-video-presets ("V10  fireflies  cartoon  624 frames at 24 fps (26.0 s)  Fireflies (cartoon)")."""
    out = subprocess.run([find_exe(args.exe), "--list-video-presets"], stdout=subprocess.PIPE, text=True).stdout
    for line in out.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1] == key:
            for field in fields:
                if field.startswith("(") and field[1:].replace(".", "", 1).isdigit():
                    return float(field[1:])
    sys.exit("unknown film %r: python scripts/anim/film.py list" % key)


def cmd_list(args):
    sys.stdout.write(subprocess.run([find_exe(args.exe), "--list-video-presets"], stdout=subprocess.PIPE, text=True).stdout)


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


def cmd_video(args):
    height = args.height or 720
    width = round(height * 16 / 9)
    cmd = [find_exe(args.exe)] + renderer_flags(args) + ["--video-preset", args.target, "--height", str(height), str(width), str(args.spp or 128), "8"]
    if args.out:
        cmd[1:1] = ["--output", args.out]
    sys.exit(subprocess.run(cmd).returncode)


def main():
    p = argparse.ArgumentParser(description="Look at a cartoon film (the films are C++: src/shared/cartoon/).")
    p.add_argument("command", choices=["list", "still", "sheet", "video"])
    p.add_argument("target", nargs="?", help="still: <key>[@seconds]; sheet and video: <key>")
    p.add_argument("--exe")
    p.add_argument("--cpu", action="store_true")
    p.add_argument("--height", type=int)
    p.add_argument("--spp", type=int)
    p.add_argument("--out")
    p.add_argument("--count", type=int, help="sheet: how many stills, evenly spaced over the film (default 16)")
    p.add_argument("--times", help="sheet: comma-separated seconds instead of --count")
    args = p.parse_args()
    if args.command != "list" and not args.target:
        p.error("%s needs a film key" % args.command)
    {"list": cmd_list, "still": cmd_still, "sheet": cmd_sheet, "video": cmd_video}[args.command](args)


if __name__ == "__main__":
    main()
