#!/usr/bin/env python3
"""photo_to_room.py - turn ONE photo of a room into a room the Scene Builder can open: its walls, floor and ceiling at the right size, the colours the photo shows, the
windows and lamps as lights, and a camera that sees what the photo saw.

This is an optional helper for the Scene Builder's "Room from a photo". It runs entirely on your own computer (no upload): a small depth model (Depth Anything V2,
the "Metric Indoor Small" fine-tune: 25 million parameters, about 100 MB, downloaded from Hugging Face on first use) guesses how far away every pixel is, and room_fit.py
(next to this file) fits a box-shaped room to that. The result is a first draft to edit, not a scan: furniture is not modelled (the room is empty), a wall the photo does
not show is assumed, and the size is only as good as the depth guess (give --camera-height when you know it).

With --furniture N it also looks for furniture: a second small model (DETR, Apache-2.0, about 170 MB) finds the chairs, sofas, tables, plants... in the photo, each is cut out and
TripoSR (the Object from a photo model) guesses a 3D mesh of it, and room_objects.py stands the mesh where the photo shows the object, as big and facing the way the photo shows it.
Each is a guess with an invented back; they are there to be moved, deleted or replaced.

Output, in --out:
    room.json   the room (see room_fit.fit_room) plus {"photo": {...}, "model": ..., "objects": [...]}
    depth.png   the depth map as a grey picture (white = near), for checking what the model saw
    object_1/ ...  with --furniture: mesh.obj and texture.png of each piece (the same files Object from a photo makes)

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
SEGMENTATION_MODEL = "facebook/detr-resnet-50-panoptic"
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


def model_folder(model=MODEL):
    """The Hugging Face cache folder of a model, whether or not it is there yet."""
    base = os.environ.get("HF_HUB_CACHE") or os.path.join(os.environ.get("HF_HOME") or os.path.join(os.path.expanduser("~"), ".cache", "huggingface"), "hub")
    return os.path.join(base, "models--" + model.replace("/", "--"))


def model_cached(model):
    snapshots = os.path.join(model_folder(model), "snapshots")
    return os.path.isdir(snapshots) and bool(os.listdir(snapshots))


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
    if model_cached(MODEL):
        fact("Room Depth Model", "present (%s)" % MODEL)
    else:
        fact("Room Depth Model", "not downloaded yet (%s, about 100 MB, fetched on first use)" % MODEL)
    try:
        import timm
        fact("Room Furniture timm", "present (%s)" % timm.__version__)
    except ImportError:
        fact("Room Furniture timm", "missing (furniture needs it; run the photo helper's setup script)")
    if model_cached(SEGMENTATION_MODEL):
        fact("Room Furniture Model", "present (%s)" % SEGMENTATION_MODEL)
    else:
        fact("Room Furniture Model", "not downloaded yet (%s, about 170 MB, fetched the first time furniture is asked for)" % SEGMENTATION_MODEL)


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


def segment_objects(image, device_choice):
    """What is in the photo, as [{"label", "score", "mask"}] (mask: a bool array the size of the photo): DETR's panoptic segmentation, which names COCO's things and some stuff."""
    import numpy as np
    import torch
    from transformers import DetrForSegmentation, DetrImageProcessor

    device = pick_device(torch, device_choice)
    processor = DetrImageProcessor.from_pretrained(SEGMENTATION_MODEL)
    model = DetrForSegmentation.from_pretrained(SEGMENTATION_MODEL).to(device).eval()
    inputs = processor(images=image, return_tensors="pt").to(device)
    with torch.no_grad():
        out = model(**inputs)
    result = processor.post_process_panoptic_segmentation(out, threshold=0.5, target_sizes=[(image.height, image.width)])[0]
    segmentation = result["segmentation"].cpu().numpy()
    segments = []
    for info in result["segments_info"]:
        segments.append({"label": model.config.id2label[info["label_id"]], "score": float(info["score"]), "mask": segmentation == info["id"]})
    del model
    if device == "cuda":
        torch.cuda.empty_cache()
    return segments


def cut_out(image, mask, margin=0.06):
    """The photo cropped to the mask's box (a little loose) with everything outside the mask transparent: what TripoSR makes an object from."""
    import numpy as np
    from PIL import Image
    ys, xs = np.nonzero(mask)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    pad_y, pad_x = int(margin * (y1 - y0)) + 2, int(margin * (x1 - x0)) + 2
    y0, y1 = max(0, y0 - pad_y), min(mask.shape[0], y1 + pad_y)
    x0, x1 = max(0, x0 - pad_x), min(mask.shape[1], x1 + pad_x)
    rgba = np.dstack([np.asarray(image.convert("RGB"))[y0:y1, x0:x1], (mask[y0:y1, x0:x1] * 255).astype(np.uint8)])
    return Image.fromarray(rgba, "RGBA")


def find_things(image, device_choice, result, base_percent=56):
    """What the segmentation model names in the photo: the segments, or None when it cannot run (a note says why)."""
    progress(base_percent, "Looking for furniture (the first time this downloads about 170 MB)")
    try:
        return segment_objects(image, device_choice)
    except ImportError as e:
        result["notes"].append("Furniture was skipped: a Python package is missing (%s); run the photo helper's setup script." % e)
        return None


def add_furniture(image, geometry, result, out_folder, segments, count, device_choice, base_percent=62, top_percent=98):
    """Makes a mesh (or measured boxes) of the furniture among `segments` and puts them in `result` ("objects", and what could not be done in "notes")."""
    import room_objects
    from PIL import Image

    def band(lo, hi):   # progress inside [lo, hi] for a step that reports 0..100
        return lambda percent, message: progress(int(lo + (hi - lo) * percent / 100.0), message)

    chosen = room_objects.choose_objects(segments, max_count=count)
    if not chosen:
        result["notes"].append("No furniture was recognised in the photo.")
        return
    names = {}
    result["objects"] = []

    def title_of(label):
        names[label] = names.get(label, 0) + 1
        return "%s %d" % (label.replace("-merged", "").replace("-", " ").capitalize(), names[label])

    # Tables and plain blocks are built from measured boxes; only what is left needs the 3D model (and its time and its download)
    remaining = []
    for seg in chosen:
        label = seg["label"]
        place = room_objects.place_object(geometry, seg["mask"], label=label)
        if label == "stairs":   # (a staircase is measured, never guessed: the 3D model cannot make steps)
            stairs = room_objects.stairs_piece(geometry, seg["mask"], title_of(label)) if place is not None else None
            if stairs:
                stairs.update({"label": label, "score": round(seg["score"], 3)})
                result["objects"].append(stairs)
            else:
                names[label] = names.get(label, 1) - 1
                result["notes"].append("A staircase was seen but could not be measured, so it is left out.")
            continue
        parts = None
        if place is not None and (label in room_objects.TABLES or label in room_objects.BLOCKS):
            title = title_of(label)
            if label in room_objects.TABLES:
                parts = room_objects.table_parts(geometry, seg["mask"], title)
            else:
                parts = room_objects.block_parts(geometry, seg["mask"], label, place, title)
            if parts:
                result["objects"].append({"name": title, "label": label, "score": round(seg["score"], 3), "kind": "boxes", "parts": parts})
                continue
            names[label] -= 1   # (no boxes came out: the title goes back, and the 3D model has a go)
        remaining.append((seg, place))
    if remaining:
        sys.path.insert(0, os.path.join(os.path.dirname(HERE), "photo_to_mesh"))
        import photo_to_mesh
        try:
            maker = photo_to_mesh.ObjectMaker(device=device_choice if device_choice != "auto" else "auto", report=band(base_percent + 4, base_percent + 8))
        except photo_to_mesh.ObjectError as e:
            result["notes"].append("Some furniture was skipped: %s" % e)
            remaining = []
    span = (top_percent - base_percent - 8) / float(max(len(remaining), 1))
    for k, (seg, place) in enumerate(remaining):
        label = seg["label"]
        lo = base_percent + 8 + span * k
        if place is None:
            result["notes"].append("The %s could not be placed (the photo gave too little depth for it)." % label)
            continue
        folder = os.path.join(out_folder, "object_%d" % (k + 1))
        try:
            cutout = cut_out(image, seg["mask"])
            os.makedirs(folder, exist_ok=True)
            cutout.save(os.path.join(folder, "cutout.png"))   # what the 3D model was shown (to see why a piece came out as it did)
            info = maker.make(cutout, folder, remove_bg=True, resolution=192, texture_size=1024, report=band(lo, lo + span))
        except photo_to_mesh.ObjectError as e:
            result["notes"].append("The %s could not be made into a 3D object: %s" % (label, e))
            continue
        except Exception as e:   # (a model failure on one piece must not lose the room)
            result["notes"].append("The %s could not be made into a 3D object (%s)." % (label, str(e).splitlines()[0][:120] if str(e) else type(e).__name__))
            continue
        exposure = float(result.get("info", {}).get("exposure", 1.0))
        if abs(exposure - 1.0) > 0.02:   # the same correction the room's surfaces got: a lit photo is not a reflectance
            import numpy as np
            texture_path = os.path.join(folder, "texture.png")
            pixels = np.asarray(Image.open(texture_path).convert("RGB"))
            Image.fromarray(room_objects.relight_texture(pixels, exposure)).save(texture_path)
        final = room_objects.finalize_object(place, info["size"], geometry["box"])
        if final is None:
            continue
        result["objects"].append({"name": title_of(label), "label": label, "score": round(seg["score"], 3),
                                  "mesh": "object_%d/mesh.obj" % (k + 1), "texture": "object_%d/texture.png" % (k + 1), **final})
    if result["objects"]:
        result["notes"].append("%d piece(s) of furniture were made from the photo (%s). Tables and blocks are measured boxes, the rest guesses with an invented back: move, resize or delete them." %
                               (len(result["objects"]), ", ".join(o["name"] for o in result["objects"])))


def add_textures(result, geometry, out_folder):
    """Gives each surface the photo's own look (room_textures.py): a picture in textures/ and "texture" in the surface. A surface the photo shows too little of keeps its flat colour."""
    import numpy as np
    from PIL import Image
    import room_textures

    folder = os.path.join(out_folder, "textures")
    made = []
    for s in result["surfaces"]:
        # a wall says which way its visible face looks (a room that is not a box has any number of walls); the floor and the ceiling are named
        spec = room_textures.face_spec(*s["face"]) if "face" in s else room_textures.FACES.get(s["name"])
        if spec is None:
            continue
        image, seen = room_textures.surface_texture(geometry["textures"], s["min"], s["max"], spec)
        if image is None:
            continue
        os.makedirs(folder, exist_ok=True)
        slug = s["name"].lower().replace(" ", "_")
        pixels = room_textures.to_reflectance_png(image, geometry["exposure"])
        # PNG rows run top to bottom: row 0 is v = 1, which is how surface_texture lays the picture out
        Image.fromarray(pixels).save(os.path.join(folder, slug + ".png"))
        s["texture"] = "textures/%s.png" % slug
        made.append(s["name"].lower())
    if made:
        result["notes"].append("The photo's own look is on the %s (read from the picture; what it does not show is filled in)." % ", ".join(made))
    # A window shows what the photo shows there (the blinds, the view): its pixels become the picture the glowing panel takes its colours from.
    windows = 0
    for light in result["lights"]:
        if "face" not in light:   # (a ceiling light has no view)
            continue
        spec = room_textures.face_spec(*light["face"])
        image, seen = room_textures.surface_texture(geometry["textures"], light["min"], light["max"], spec, texels_per_metre=240, max_size=512, min_seen=0.5, check_depth=False)
        if image is None:
            continue
        os.makedirs(folder, exist_ok=True)
        windows += 1
        slug = "window_%d" % windows
        # (no exposure correction: the picture is light, not a reflectance; the panel's strength sets how much)
        Image.fromarray((np.clip(image, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)).save(os.path.join(folder, slug + ".png"))
        light["picture"] = "textures/%s.png" % slug
    if windows:
        result["notes"].append("%d window(s) show what the photo shows there." % windows)


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
    ap.add_argument("--box", action="store_true", help="keep the room a plain box (the default also reads an L, an alcove or a recess off the photo's walls)")
    ap.add_argument("--furniture", type=int, default=0, metavar="N", help="also find up to N pieces of furniture in the photo and make a 3D object of each (slower: a model per piece)")
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

    progress(55 if args.furniture > 0 else 75, "Fitting the room")
    try:
        result, geometry = room_fit.fit_room(depth, np.asarray(image), hfov, camera_height=args.camera_height, return_geometry=True, allow_outline=not args.box)
    except ValueError as e:
        fail("could not find a room in this photo (%s). A photo taken inside a room, showing a floor and at least one wall, works best." % e)
    segments = find_things(image, args.device, result) if args.furniture > 0 else None
    if segments:
        import room_objects
        # what stands in the room is left out of the walls' and floor's pictures (it is modelled, or at least not part of the surface)
        geometry["textures"]["exclude"] = room_objects.object_mask(segments)
    add_textures(result, geometry, args.out)
    if segments:
        add_furniture(image, geometry, result, args.out, segments, args.furniture, args.device)
    result["photo"] = {"width": image.width, "height": image.height, "hfov": round(hfov, 3), "fov_source": fov_source,
                       "brightness": round(room_fit.picture_brightness(np.asarray(image)), 4)}   # (the Scene Builder renders a draft and sets the lights to match this)
    result["model"] = MODEL
    with open(os.path.join(args.out, "room.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    progress(100, "Done")


if __name__ == "__main__":
    main()
