#!/usr/bin/env python3
"""Repacks one glTF model (.glb, or .gltf with its .bin and pictures beside it) as a slim single .glb, the way the files in models/gltf/ were made.

  python3 scripts/pack_glb.py <in.glb|in.gltf> <out.glb> [--max-pixels 1024]

What changes (models/gltf/CREDITS.md says the same, because the licences of the models ask for a note of any change):
  * the buffers and the pictures are put into the one file;
  * only the base-colour pictures are kept, each at most --max-pixels across (JPEG, or PNG where the picture is really see-through);
    normal, occlusion, emissive and the like are dropped (the importer, src/shared/gltf_import.h, does not read them);
  * the average of a metal-roughness picture (green = roughness, blue = metal) goes into the material's metallicFactor and roughnessFactor, and the picture is dropped;
  * an emissive factor that multiplied an emissive picture is dropped with the picture (alone it would make the whole surface glow).
The file is read as data only. Needs Pillow. The sources of the models in the library: github.com/KhronosGroup/glTF-Sample-Assets (Models/<name>/glTF-Binary/<name>.glb),
polyhaven.com (api.polyhaven.com/files/<id>: the 1k glTF with its includes) and kenney.nl (the Furniture Kit's Models/GLTF format).
"""
import io
import json
import os
import struct
import sys

from PIL import Image


def read_glb(data):
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2 or length > len(data):
        raise SystemExit("not a glb")
    pos, js, binary = 12, None, b""
    while pos + 8 <= length:
        size, kind = struct.unpack_from("<II", data, pos)
        chunk = data[pos + 8:pos + 8 + size]
        if kind == 0x4E4F534A and js is None:
            js = json.loads(chunk.decode("utf-8"))
        elif kind == 0x004E4942 and not binary:
            binary = chunk
        pos += 8 + ((size + 3) & ~3)
    return js, binary


def load_source(path):
    folder = os.path.dirname(os.path.abspath(path))
    if path.lower().endswith(".glb"):
        js, binary = read_glb(open(path, "rb").read())
        return js, [binary], folder
    js = json.load(open(path, encoding="utf-8"))
    buffers = []
    for b in js.get("buffers", []):
        uri = b["uri"]
        if uri.startswith("data:") or ".." in uri or os.path.isabs(uri):
            raise SystemExit("a buffer outside the model's folder: " + uri)
        buffers.append(open(os.path.join(folder, uri), "rb").read())
    return js, buffers, folder


def image_bytes(js, buffers, folder, image):
    if "bufferView" in image:
        v = js["bufferViews"][image["bufferView"]]
        off = v.get("byteOffset", 0)
        return buffers[v["buffer"]][off:off + v["byteLength"]]
    uri = image["uri"]
    if uri.startswith("data:") or ".." in uri or os.path.isabs(uri):
        raise SystemExit("a picture outside the model's folder: " + uri)
    return open(os.path.join(folder, uri.replace("%20", " ")), "rb").read()


def strip_textures(node, parent):
    """Removes every *Texture entry of a material except the base-colour one."""
    if isinstance(node, dict):
        for k in list(node.keys()):
            if k.endswith("Texture") and (parent, k) != ("pbrMetallicRoughness", "baseColorTexture"):
                del node[k]
            else:
                strip_textures(node[k], k if k == "pbrMetallicRoughness" else parent)
    elif isinstance(node, list):
        for x in node:
            strip_textures(x, parent)


def pack(path, out_path, max_pixels=1024):
    js, buffers, folder = load_source(path)
    js.pop("extensionsRequired", None)
    used_ext = set(js.get("extensionsUsed", []))
    used_ext.discard("KHR_texture_transform")
    for m in js.get("materials", []):
        pbr = m.get("pbrMetallicRoughness") or {}
        mr = pbr.get("metallicRoughnessTexture")
        if mr is not None:
            src = js.get("textures", [])[mr["index"]].get("source")
            if src is not None:
                im = Image.open(io.BytesIO(image_bytes(js, buffers, folder, js["images"][src]))).convert("RGB").resize((32, 32), Image.LANCZOS)
                px = list(im.tobytes())
                n = len(px) // 3
                pbr["roughnessFactor"] = round(pbr.get("roughnessFactor", 1.0) * sum(px[1::3]) / (255.0 * n), 4)
                pbr["metallicFactor"] = round(pbr.get("metallicFactor", 1.0) * sum(px[2::3]) / (255.0 * n), 4)
                m["pbrMetallicRoughness"] = pbr
        if "emissiveTexture" in m:
            m.pop("emissiveFactor", None)
        strip_textures(m, "")
        for e in (m.get("extensions") or {}).values():
            if isinstance(e, dict):
                e.pop("texCoord", None)
    textures = js.get("textures", [])
    new_textures, tex_map = [], {}
    for m in js.get("materials", []):
        t = (m.get("pbrMetallicRoughness") or {}).get("baseColorTexture")
        if not t:
            continue
        if t["index"] not in tex_map:
            src = textures[t["index"]].get("source")
            if src is None:
                del m["pbrMetallicRoughness"]["baseColorTexture"]
                continue
            tex_map[t["index"]] = len(new_textures)
            new_textures.append({"source": src})
        t["index"] = tex_map[t["index"]]
        t.pop("extensions", None)
    images_old = js.get("images", [])
    new_images, pictures, image_new_index = [], [], {}
    for old in sorted({t["source"] for t in new_textures}):
        im = Image.open(io.BytesIO(image_bytes(js, buffers, folder, images_old[old])))
        im.load()
        if max(im.size) > max_pixels:
            s = max_pixels / max(im.size)
            im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
        has_alpha = im.mode in ("RGBA", "LA") and im.getchannel("A").getextrema()[0] < 250
        out = io.BytesIO()
        if has_alpha:
            im.convert("RGBA").save(out, "PNG", optimize=True)
            mime = "image/png"
        else:
            im.convert("RGB").save(out, "JPEG", quality=85, optimize=True)
            mime = "image/jpeg"
        image_new_index[old] = len(new_images)
        new_images.append({"mimeType": mime, "name": images_old[old].get("name", "")})
        pictures.append(out.getvalue())
    for t in new_textures:
        t["source"] = image_new_index[t["source"]]
    image_views = {im["bufferView"] for im in images_old if "bufferView" in im}
    new_bin, view_map, new_views = bytearray(), {}, []
    for i, v in enumerate(js.get("bufferViews", [])):
        if i in image_views:
            continue
        while len(new_bin) % 4:
            new_bin.append(0)
        off = v.get("byteOffset", 0)
        nv = dict(v)
        nv["buffer"], nv["byteOffset"] = 0, len(new_bin)
        view_map[i] = len(new_views)
        new_views.append(nv)
        new_bin += buffers[v["buffer"]][off:off + v["byteLength"]]
    for a in js.get("accessors", []):
        if "bufferView" in a:
            a["bufferView"] = view_map[a["bufferView"]]
        if "sparse" in a:
            for key in ("indices", "values"):
                a["sparse"][key]["bufferView"] = view_map[a["sparse"][key]["bufferView"]]
    for k, pic in enumerate(pictures):
        while len(new_bin) % 4:
            new_bin.append(0)
        new_images[k]["bufferView"] = len(new_views)
        new_views.append({"buffer": 0, "byteOffset": len(new_bin), "byteLength": len(pic)})
        new_bin += pic
    js["bufferViews"], js["images"], js["textures"] = new_views, new_images, new_textures
    for key in ("images", "textures"):
        if not js[key]:
            del js[key]
    js["buffers"] = [{"byteLength": len(new_bin)}]
    if used_ext:
        js["extensionsUsed"] = sorted(used_ext)
    else:
        js.pop("extensionsUsed", None)
    text = json.dumps(js, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    while len(new_bin) % 4:
        new_bin.append(0)
    glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(new_bin))
    glb += struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(new_bin), 0x004E4942) + bytes(new_bin)
    open(out_path, "wb").write(glb)
    return len(glb)


if __name__ == "__main__":
    argv = sys.argv[1:]
    limit = 1024
    if "--max-pixels" in argv:
        i = argv.index("--max-pixels")
        limit = int(argv[i + 1])
        del argv[i:i + 2]
    if len(argv) != 2:
        raise SystemExit(__doc__)
    print("%s: %d bytes" % (argv[1], pack(argv[0], argv[1], limit)))
