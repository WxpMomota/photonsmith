#!/usr/bin/env python3
"""photo_to_room.py - turn ONE photo of a room into a room the Scene Builder can open: its walls, floor and ceiling at the right size, the colours the photo shows, the
windows and lamps as lights, and a camera that sees what the photo saw.

This is an optional helper for the Scene Builder's "Room from a photo". It runs entirely on your own computer (no upload): a small depth model (Depth Anything V2,
the "Metric Indoor Small" fine-tune: 25 million parameters, about 100 MB, downloaded from Hugging Face on first use) guesses how far away every pixel is, and room_fit.py
(next to this file) fits a box-shaped room to that. The result is a first draft to edit, not a scan: furniture is not modelled (the room is empty), a wall the photo does
not show is assumed, and the size is only as good as the depth guess (give --camera-height when you know it).

Output, in --out:
    room.json   the room (see room_fit.fit_room) plus {"photo": {...}, "model": ...}
    depth.png   the depth map as a grey picture (white = near), for checking what the model saw

While it runs it prints lines "PROGRESS <percent> <message>" so a caller can show a progress bar, and "ERROR <message>" / "NOTE <message>" lines.

Needs the Python environment made by scripts/setup_photo_to_mesh.ps1 (or .sh): PyTorch, transformers and Pillow are all it uses.
"""
import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

MODEL = "depth-anything/Depth-Anything-V2-Metric-Indoor-Small-hf"
DEFAULT_F35 = 26.0   # the 35 mm equivalent focal length assumed when the photo has none: a phone's main camera
EXIF_FOCAL_35 = 41989
EXIF_IFD = 0x8769

os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")


def progress(percent, message):
    print("PROGRESS %d %s" % (percent, message), flush=True)


def fail(message, code=2):
    print("ERROR " + message, flush=True)
    sys.exit(code)


def horizontal_fov(width, height, f35):
    """The horizontal field of view in degrees of a photo of `width` x `height` pixels taken at a 35 mm equivalent focal length of f35 (a 35 mm frame's diagonal is
    43.27 mm; the equivalent focal length is defined by the diagonal, so the half-width in the same units is diagonal-fraction times 21.63 / f35)."""
    diagonal = math.hypot(width, height)
    return math.degrees(2.0 * math.atan((width / diagonal) * 21.635 / f35))


def photo_fov(image, width, height, override):
    """(hfov, where it came from): --fov, else the photo's own EXIF 35 mm focal length, else an assumed phone camera."""
    if override:
        return float(override), "given"
    try:
        f35 = image.getexif().get_ifd(EXIF_IFD).get(EXIF_FOCAL_35)
        if f35 and float(f35) > 0:
            return horizontal_fov(width, height, float(f35)), "the photo's EXIF (%.0f mm equivalent)" % float(f35)
    except Exception:
        pass
    return horizontal_fov(width, height, DEFAULT_F35), "assumed (a phone camera, %.0f mm equivalent)" % DEFAULT_F35


def model_folder():
    """The Hugging Face cache folder of the depth model, whether or not it is there yet."""
    base = os.environ.get("HF_HUB_CACHE") or os.path.join(os.environ.get("HF_HOME") or os.path.join(os.path.expanduser("~"), ".cache", "huggingface"), "hub")
    return os.path.join(base, "models--" + MODEL.replace("/", "--"))


def check_environment():
    """One "Key: value" fact per line for the Diagnostics tab ("present" is good; "missing" and "not usable" are problems)."""
    def fact(key, value):
        print("%s: %s" % (key, value), flush=True)

    fact("Room Helper Script", "present (%s)" % os.path.join(HERE, "photo_to_room.py"))
    try:
        import numpy
        fact("Room Helper NumPy", "present (%s)" % numpy.__version__)
    except ImportError:
        fact("Room Helper NumPy", "missing")
    try:
        import transformers
        fact("Room Helper Transformers", "present (%s)" % transformers.__version__)
    except ImportError:
        fact("Room Helper Transformers", "missing")
    folder = model_folder()
    if os.path.isdir(os.path.join(folder, "snapshots")) and os.listdir(os.path.join(folder, "snapshots")):
        fact("Room Depth Model", "present (%s)" % MODEL)
    else:
        fact("Room Depth Model", "not downloaded yet (%s, about 100 MB, fetched on first use)" % MODEL)


def pick_device(torch, choice):
    if choice != "auto":
        return choice
    if torch.cuda.is_available():
        return "cuda"
    if getattr(torch.backends, "mps", None) is not None and torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def estimate_depth(image, device_choice):
    """Metric depth (metres, a float64 (H, W) array at the photo's size) from the depth model."""
    import numpy as np
    import torch
    from transformers import AutoImageProcessor, AutoModelForDepthEstimation

    device = pick_device(torch, device_choice)
    progress(15, "Loading the depth model (the first time this downloads about 100 MB)")
    processor = AutoImageProcessor.from_pretrained(MODEL)
    model = AutoModelForDepthEstimation.from_pretrained(MODEL).to(device).eval()
    progress(45, "Measuring distances (%s)" % device)
    inputs = processor(images=image, return_tensors="pt")
    inputs = {k: v.to(device) for k, v in inputs.items()}
    with torch.no_grad():
        out = model(**inputs).predicted_depth
    h, w = image.height, image.width
    depth = torch.nn.functional.interpolate(out.unsqueeze(1).float(), size=(h, w), mode="bicubic", align_corners=False)[0, 0]
    return depth.cpu().numpy().astype(np.float64), device


def write_depth_picture(depth, path):
    import numpy as np
    from PIL import Image
    near = np.clip(1.0 - depth / max(float(np.percentile(depth, 99)), 1e-6), 0.0, 1.0)
    Image.fromarray((near * 255).astype(np.uint8)).save(path)


def main():
    ap = argparse.ArgumentParser(description="Turn a photo of a room into a room for the Scene Builder.")
    ap.add_argument("photo", nargs="?")
    ap.add_argument("--out", help="the folder the result goes in")
    ap.add_argument("--fov", type=float, help="the photo's horizontal field of view in degrees (default: from the photo's EXIF, else a phone camera's)")
    ap.add_argument("--camera-height", type=float, help="how high the camera was above the floor, in metres (default: the depth model's own estimate)")
    ap.add_argument("--device", default="auto", choices=["auto", "cuda", "mps", "cpu"])
    ap.add_argument("--check", action="store_true", help="print what the helper needs and what is missing, then exit")
    args = ap.parse_args()

    if args.check:
        check_environment()
        return
    if not args.photo or not args.out:
        fail("usage: photo_to_room.py PHOTO --out FOLDER")
    if not os.path.isfile(args.photo):
        fail("the photo %s does not exist" % args.photo)
    try:
        import numpy as np
        from PIL import Image, ImageOps
    except ImportError as e:
        fail("a Python package is missing (%s); run the photo helper's setup script" % e)
    import room_fit

    os.makedirs(args.out, exist_ok=True)
    progress(3, "Reading the photo")
    try:
        original = Image.open(args.photo)
        original.load()
    except Exception as e:
        fail("could not read the photo (%s)" % e)
    # The lens comes from the file as it is; the picture is then turned upright the way the camera says.
    hfov, fov_source = None, None
    image = ImageOps.exif_transpose(original).convert("RGB")
    if max(image.size) > 1600:
        scale = 1600.0 / max(image.size)
        image = image.resize((max(1, round(image.width * scale)), max(1, round(image.height * scale))), Image.LANCZOS)
    hfov, fov_source = photo_fov(original, image.width, image.height, args.fov)
    if not (20.0 <= hfov <= 140.0):
        fail("the field of view %.1f degrees is not believable; give --fov (the photo's horizontal angle of view)" % hfov)
    print("NOTE field of view %.1f degrees, %s" % (hfov, fov_source), flush=True)

    try:
        depth, device = estimate_depth(image, args.device)
    except Exception as e:
        if "out of memory" in str(e).lower():
            fail("ran out of graphics memory; try again with --device cpu")
        fail("the depth model failed (%s)" % e)
    write_depth_picture(depth, os.path.join(args.out, "depth.png"))

    progress(75, "Fitting the room")
    try:
        result = room_fit.fit_room(depth, np.asarray(image), hfov, camera_height=args.camera_height)
    except ValueError as e:
        fail("could not find a room in this photo (%s). A photo taken inside a room, showing a floor and at least one wall, works best." % e)
    result["photo"] = {"width": image.width, "height": image.height, "hfov": round(hfov, 3), "fov_source": fov_source,
                       "brightness": round(room_fit.picture_brightness(np.asarray(image)), 4)}   # (the Scene Builder renders a draft and sets the lights to match this)
    result["model"] = MODEL
    with open(os.path.join(args.out, "room.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    progress(100, "Done")


if __name__ == "__main__":
    main()
