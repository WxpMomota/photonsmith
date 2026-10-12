#!/usr/bin/env python3
"""Consistency sweep: the same scene written two ways must give the same picture.

A renderer can agree with another renderer and still be wrong, because both can share a mistake. It cannot disagree with ITSELF about two descriptions of the same
thing. This renders pairs of scenes that must look alike, on each backend, and reports the pairs that do not:

  sky         a constant sky ("rgb L") against the same sky as a white picture, for every material
  translate   the whole scene moved by an arbitrary offset
  scale       the whole scene made 8 times bigger (distances, radii, light intensities)
  mirror      the whole scene mirrored left to right
  lightform   a point light against a small emissive sphere of the same intensity
  size        the same scene at two picture sizes
  spp         the same scene at 16 and 128 samples a pixel

The means are compared, per colour channel, within a tolerance. Needs a built `ray_tracer` (build_macos/ray_tracer by default; --gpu is Metal on a Mac) and nothing else:
the EXR reader is in this file.

  python3 scripts/consistency_sweep.py [--exe PATH] [--backends cpu,gpu] [--only sky,translate,...] [--tolerance 4] [-v]

Exit code 1 when a pair is off by more than the tolerance. See docs/CONSISTENCY_SWEEP.md.
"""
import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

_LINES = {0: 1, 2: 1, 3: 16}


def read_exr_means(path):
    """Mean of each of R, G, B of a scanline OpenEXR file (none, ZIPS or ZIP compression, float or half channels are not needed: ray_tracer writes float)."""
    b = open(path, "rb").read()
    assert b[:4] == b"v/1\x01", "not an OpenEXR file"
    i = 8
    attrs = {}
    while b[i] != 0:
        j = b.index(b"\0", i); name = b[i:j].decode(); i = j + 1
        j = b.index(b"\0", i); i = j + 1
        size = struct.unpack("<I", b[i:i + 4])[0]; i += 4
        attrs[name] = b[i:i + size]; i += size
    pos = i + 1
    ch = attrs["channels"]; names = []; k = 0
    while ch[k] != 0:
        j = ch.index(b"\0", k); names.append(ch[k:j].decode()); k = j + 1 + 16
    comp = attrs["compression"][0]; lpb = _LINES[comp]
    x0, y0, x1, y1 = struct.unpack("<4i", attrs["dataWindow"])
    W, H = x1 - x0 + 1, y1 - y0 + 1
    nb = (H + lpb - 1) // lpb
    offs = struct.unpack("<%dQ" % nb, b[pos:pos + 8 * nb])
    sums = {n: 0.0 for n in names}
    for off in offs:
        y, size = struct.unpack("<ii", b[off:off + 8]); data = b[off + 8:off + 8 + size]
        rows = min(lpb, H - (y - y0)); raw = rows * len(names) * W * 4
        if comp != 0 and size < raw:
            data = zlib.decompress(data)
            a = bytearray(data)
            for t in range(1, len(a)):
                a[t] = (a[t - 1] + a[t] - 128) & 0xFF
            half = (len(a) + 1) // 2
            d = bytearray(len(a)); d[0::2] = a[:half]; d[1::2] = a[half:]; data = bytes(d)
        vals = struct.unpack("<%df" % (rows * len(names) * W), data)
        p = 0
        for _ in range(rows):
            for n in names:
                sums[n] += sum(vals[p:p + W]); p += W
    return [sums[c] / (W * H) for c in ("R", "G", "B")]


# ---- the scene: a floor, a ball of the material under test and a lamp, in a sky --------------------------------------------------------------------------------

MATERIALS = {
    "diffuse": 'Material "diffuse" "rgb reflectance" [ 0.6 0.4 0.3 ]',
    "conductor": 'Material "conductor" "rgb reflectance" [ 0.9 0.6 0.4 ] "float roughness" [ 0.1 ]',
    "dielectric": 'Material "dielectric" "float eta" [ 1.5 ]',
    "roughdielectric": 'Material "dielectric" "float eta" [ 1.5 ] "float roughness" [ 0.2 ]',
    "thindielectric": 'Material "thindielectric" "float eta" [ 1.5 ]',
    "coateddiffuse": 'Material "coateddiffuse" "rgb reflectance" [ 0.5 0.3 0.2 ] "float roughness" [ 0.1 ]',
    "coatedconductor": 'Material "coatedconductor" "rgb reflectance" [ 0.9 0.7 0.5 ] "float interface.roughness" [ 0.05 ] "float conductor.roughness" [ 0.1 ]',
    "conductor_cu": 'Material "conductor" "spectrum eta" "metal-Cu-eta" "spectrum k" "metal-Cu-k" "float roughness" [ 0.05 ]',
    "measured": 'Material "measured" "string filename" [ "%s" ]' % os.path.join(REPO, "pbrt_scenes", "synthetic-gold.bsdf").replace("\\", "/"),
    "diffusetransmission": 'Material "diffusetransmission" "rgb reflectance" [ 0.5 0.4 0.3 ] "rgb transmittance" [ 0.3 0.3 0.3 ]',
    # the mean free path is a length: scaling the scene scales it too (or the ball is a different ball)
    "subsurface": lambda sc: 'Material "subsurface" "rgb mfp" [ %g %g %g ] "rgb reflectance" [ 0.8 0.8 0.8 ] "float eta" [ 1.33 ] "float roughness" [ 0 ]' % ((0.4 * sc.scale,) * 3),
    # participating media: a sphere of "interface" material holding the medium (sigma is per unit length, so it scales with 1/scale)
    "fog": lambda sc: 'Material "interface"\n  MediumInterface "m" ""',
    "cloud": lambda sc: 'Material "interface"\n  MediumInterface "puffcloud" ""',
    "rgbgrid": lambda sc: 'Material "interface"\n  MediumInterface "grid" ""',
    "uniformgrid": lambda sc: 'Material "interface"\n  MediumInterface "ugrid" ""',
}

# materials whose light paths go through a specular chain a point light cannot reach (a point light cannot be hit by a random walk through glass, so the caustic a sphere
# lamp makes is not there): the point-against-sphere-lamp pair is not a fair one for them
SPECULAR = {"dielectric", "roughdielectric", "thindielectric"}
ROTATE_MATERIALS = {"diffuse", "conductor", "coateddiffuse", "dielectric", "subsurface"}
SHAPE_MATERIALS = {"diffuse", "conductor", "coateddiffuse", "dielectric", "fog"}
LIGHT_MATERIALS = {"diffuse", "conductor", "coateddiffuse", "diffusetransmission", "subsurface"}
BACKEND_TOLERANCE = {"uniformgrid": 8.0}   # a material whose two backends are known to differ by more than the default 6%: Metal draws a uniformgrid with its RGB grid medium and reads 5% brighter than the CPU (rgbgrid: 3 to 4%)
SHAPE_LIST = ("cylinder", "disk", "cone", "paraboloid", "cube")
CAMERA_MATERIALS = ("diffuse", "coateddiffuse", "dielectric")
SUN_MATERIALS = ("diffuse", "conductor", "coateddiffuse")
NOISY = {"dielectric", "thindielectric"}   # fireflies: a pair needs a looser tolerance


def medium_prelude(sc):
    """MakeNamedMedium for the media 'materials', placed in the scene's own (scaled, moved, mirrored) frame."""
    s_ = sc.scale
    if sc.material == "fog":
        return 'MakeNamedMedium "m" "string type" [ "homogeneous" ] "rgb sigma_a" [ %g %g %g ] "rgb sigma_s" [ %g %g %g ] "float g" [ 0.3 ]' % ((0.05 / s_,) * 3 + (0.6 / s_,) * 3)
    if sc.material == "cloud":
        # a cloud whose box (2.4 across) is bigger than its interface sphere (radius 1): the sphere clips it
        c = sc.p(0, 0, 0) if sc.fill else sc.p(0.5, 1.0, 0)
        return ('AttributeBegin\n  Translate %s\n  Scale %g %g %g\n  Translate -0.5 -0.5 -0.5\n  MakeNamedMedium "puffcloud" "string type" [ "cloud" ] "rgb sigma_a" [ 0 0 0 ] "rgb sigma_s" [ %g %g %g ] '
                '"float g" [ 0.3 ] "float density" [ 1 ] "float wispiness" [ 1 ] "float frequency" [ 4 ]\nAttributeEnd') % (sc.f(c), 2.4 * s_, 2.4 * s_, 2.4 * s_, 0.5 / s_, 0.5 / s_, 0.5 / s_)
    if sc.material == "uniformgrid":
        # a 2x2x2 grid of different densities scaling one sigma_s, its box bigger than the interface sphere (the sphere clips it)
        c = sc.p(0, 0, 0) if sc.fill else sc.p(0.5, 1.0, 0)
        dens = [0.4, 1.0, 0.7, 1.6, 0.2, 1.2, 0.9, 1.4]
        return ('AttributeBegin\n  Translate %s\n  Scale %g %g %g\n  Translate -0.5 -0.5 -0.5\n  MakeNamedMedium "ugrid" "string type" [ "uniformgrid" ] "integer nx" [ 2 ] "integer ny" [ 2 ] "integer nz" [ 2 ] '
                '"float g" [ 0.3 ] "rgb sigma_s" [ %g %g %g ] "float density" [ %s ]\nAttributeEnd') % (sc.f(c), 2.4 * s_, 2.4 * s_, 2.4 * s_, 0.5 / s_, 0.7 / s_, 0.9 / s_, " ".join("%g" % d for d in dens))
    if sc.material == "rgbgrid":
        # a 2x2x2 grid of different scattering and absorption per voxel and colour, its box bigger than the interface sphere (the sphere clips it)
        c = sc.p(0, 0, 0) if sc.fill else sc.p(0.5, 1.0, 0)
        ss = [0.2, 0.35, 0.5, 0.6, 0.3, 0.1, 0.4, 0.4, 0.4, 0.1, 0.5, 0.9, 0.7, 0.2, 0.3, 0.3, 0.6, 0.2, 0.5, 0.5, 0.1, 0.2, 0.8, 0.4]
        sa = [0.0, 0.05, 0.1, 0.02, 0.0, 0.0, 0.05, 0.05, 0.05, 0.0, 0.1, 0.0, 0.0, 0.0, 0.1, 0.1, 0.0, 0.0, 0.0, 0.2, 0.0, 0.0, 0.0, 0.0]
        return ('AttributeBegin\n  Translate %s\n  Scale %g %g %g\n  Translate -0.5 -0.5 -0.5\n  MakeNamedMedium "grid" "string type" [ "rgbgrid" ] "integer nx" [ 2 ] "integer ny" [ 2 ] "integer nz" [ 2 ] '
                '"float g" [ 0.3 ] "rgb sigma_a" [ %s ] "rgb sigma_s" [ %s ]\nAttributeEnd') % (sc.f(c), 2.4 * s_, 2.4 * s_, 2.4 * s_, " ".join("%g" % (v / s_) for v in sa), " ".join("%g" % (v / s_) for v in ss))
    return ""


class Scene:
    """A scene description with the knobs the sweep turns: offset, scale, mirror, turn about the vertical axis, sky form, lamp form, ball form, floor form, size, samples."""

    def __init__(self, material="diffuse", sky="constant", lamp="area", offset=(0, 0, 0), scale=1.0, mirror=False, rot=0.0, size=48, spp=64, fill=False, depth=8, sky_png=None,
                 sky_scale=1.0, ball="sphere", floor="tri", material_text=None, prelude="", camera="perspective", shape="sphere", lamp_png=None, sampler="halton", integrator="volpath", film_extra=""):
        self.__dict__.update(locals())
        del self.__dict__["self"]

    def p(self, x, y, z):
        """A point of the base scene, mirrored, scaled, turned about the vertical axis and moved."""
        if self.mirror:
            x = -x
        s = self.scale
        c, sn = math.cos(math.radians(self.rot)), math.sin(math.radians(self.rot))
        x, z = c * x + sn * z, -sn * x + c * z   # pbrt's Rotate about +y
        return (x * s + self.offset[0], y * s + self.offset[1], z * s + self.offset[2])

    @staticmethod
    def f(v):
        return " ".join("%.9g" % c for c in v)

    def sky_text(self):
        if self.sky == "constant":
            return 'LightSource "infinite" "rgb L" [ %s ]' % ("%g %g %g" % ((self.sky_scale,) * 3) if self.fill else "%g %g %g" % (0.25 * self.sky_scale, 0.25 * self.sky_scale, 0.3 * self.sky_scale))
        light = 'LightSource "infinite" "string filename" [ "%s" ] "float scale" [ %g ]' % (self.sky_png, self.sky_scale)
        if self.rot:
            return "AttributeBegin\n  Rotate %g 0 1 0\n  %s\nAttributeEnd" % (self.rot, light)   # the picture turns with the scene
        return light

    def lamp_text(self):
        s, p, f = self.scale, self.p, self.f
        lamp_at, power = p(-1.2, 3.0, 1.0), 40.0
        if self.lamp == "none":   # lit by the sky alone
            return ""
        if self.lamp == "area":
            return 'AttributeBegin\n  Translate %s\n  AreaLightSource "diffuse" "rgb L" [ %s ]\n  Shape "sphere" "float radius" [ %g ]\nAttributeEnd' % (f(lamp_at), f((power / (3.14159265 * 0.04),) * 3), 0.2 * s)
        if self.lamp == "texarea":   # the same sphere light, its emission scaled by a white picture
            return 'AttributeBegin\n  Translate %s\n  AreaLightSource "diffuse" "string filename" [ "%s" ] "float scale" [ %g ]\n  Shape "sphere" "float radius" [ %g ]\nAttributeEnd' % (f(lamp_at), self.lamp_png, power / (3.14159265 * 0.04), 0.2 * s)
        if self.lamp == "point":
            return 'LightSource "point" "point3 from" [ %s ] "rgb I" [ %s ]' % (f(lamp_at), f((power * s * s,) * 3))
        if self.lamp == "spot":   # a cone wide enough to hold the whole scene, with a hard edge: lights what the point light does
            return 'LightSource "spot" "point3 from" [ %s ] "point3 to" [ %s ] "float coneangle" [ 89 ] "float conedeltaangle" [ 0 ] "rgb I" [ %s ]' % (f(lamp_at), f(p(-1.2, 0.0, 1.0)), f((power * s * s,) * 3))
        # pbrt-v4's goniometric and projection lights have no "from": they sit where the current transform puts them (a projection light shines along +z)
        if self.lamp == "gonio":   # a goniometric light with no picture is a point light
            return 'AttributeBegin\n  Translate %s\n  LightSource "goniometric" "rgb I" [ %s ]\nAttributeEnd' % (f(lamp_at), f((power * s * s,) * 3))
        if self.lamp == "projection":   # a wide frustum with no picture, pointing down: a point light over the whole scene (the projection light's strength is its "scale")
            return 'AttributeBegin\n  Translate %s\n  Rotate 90 1 0 0\n  LightSource "projection" "float fov" [ 170 ] "float scale" [ %g ]\nAttributeEnd' % (f(lamp_at), power * s * s)
        # a distant light of irradiance E, and the same light as a point light 1000 units away along its direction (intensity E * d^2)
        d = (-0.3, 0.85, 0.25)
        n = math.sqrt(sum(c * c for c in d))
        d = tuple(c / n for c in d)
        base = (0.5, 1.0, 0.0)
        e = 2.0
        if self.lamp == "distant":
            return 'LightSource "distant" "point3 from" [ %s ] "point3 to" [ %s ] "rgb L" [ %s ]' % (f(p(*(b + c for b, c in zip(base, d)))), f(p(*base)), f((e,) * 3))
        far = 1000.0
        return 'LightSource "point" "point3 from" [ %s ] "rgb I" [ %s ]' % (f(p(*(b + c * far for b, c in zip(base, d)))), f((e * (far * s) ** 2,) * 3))

    def floor_text(self):
        p, f = self.p, self.f
        q = [p(-6, 0, -6), p(6, 0, -6), p(6, 0, 6), p(-6, 0, 6)]
        if self.mirror:
            q = [q[1], q[0], q[3], q[2]]   # the mirrored floor keeps its winding (its normal stays up), so the pair differs only by the mirror
        shape = 'Shape "trianglemesh" "integer indices" [ 0 2 1 0 3 2 ] "point3 P" [ %s ]'
        if self.floor == "tri2":   # the other diagonal
            shape = 'Shape "trianglemesh" "integer indices" [ 0 3 1 1 3 2 ] "point3 P" [ %s ]'
        elif self.floor == "bilinear":   # one planar patch: p00 p10 p01 p11
            shape = 'Shape "bilinearmesh" "integer indices" [ 0 1 3 2 ] "point3 P" [ %s ]'
        elif self.floor == "bilinear_plain":   # the same patch with its four points listed in the patch's own order
            q = [q[0], q[1], q[3], q[2]]
            shape = 'Shape "bilinearmesh" "point3 P" [ %s ]'
        return 'AttributeBegin\n  Material "diffuse" "rgb reflectance" [ 0.7 0.7 0.7 ]\n  %s\nAttributeEnd' % (shape % " ".join(f(v) for v in q))

    SHAPES = {
        # a bundle of strands, a thin tube each: for the hair material
        "hair": "\n  ".join('Shape "curve" "point3 P" [ %g %g %g  %g %g %g  %g %g %g  %g %g %g ] "float width" [ 0.06 ]' % (
            0.0, -0.8, 0.0,  0.5 * math.cos(a), -0.2, 0.5 * math.sin(a),  0.7 * math.cos(a), 0.3, 0.7 * math.sin(a),  0.3 * math.cos(a), 0.9, 0.3 * math.sin(a)) for a in [i * math.pi / 4 for i in range(8)]),
        "cylinder": 'Shape "cylinder" "float radius" [ 0.6 ] "float zmin" [ -0.6 ] "float zmax" [ 0.6 ]',
        "disk": 'Shape "disk" "float radius" [ 1 ]',
        "cone": 'Shape "cone" "float radius" [ 0.8 ] "float height" [ 1.2 ]',
        "paraboloid": 'Shape "paraboloid" "float radius" [ 0.8 ] "float zmax" [ 1.0 ]',
        # a square facing the camera with texture coordinates 0..1 (the shapes above make their own): for a picture looked up through a mapping
        "uvquad": 'Shape "trianglemesh" "integer indices" [ 0 1 2 0 2 3 ] "point3 P" [ -0.8 -0.8 0  0.8 -0.8 0  0.8 0.8 0  -0.8 0.8 0 ] "point2 uv" [ 0 0  1 0  1 1  0 1 ]',
        "cube": 'Shape "trianglemesh" "integer indices" [ 0 2 1 0 3 2  4 5 6 4 6 7  0 1 5 0 5 4  2 3 7 2 7 6  0 4 7 0 7 3  1 2 6 1 6 5 ] '
                '"point3 P" [ -0.7 -0.7 -0.7  0.7 -0.7 -0.7  0.7 0.7 -0.7  -0.7 0.7 -0.7  -0.7 -0.7 0.7  0.7 -0.7 0.7  0.7 0.7 0.7  -0.7 0.7 0.7 ]',
    }

    def ball_text(self, material):
        p, f, s = self.p, self.f, self.scale
        at = p(0, 0, 0) if self.fill else p(0.5, 1.0, 0)
        if self.shape != "sphere" and self.ball == "instance":   # the same, as an object instance
            return 'ObjectBegin "ball"\n  %s\n  %s\nObjectEnd\nAttributeBegin\n  Translate %s\n  Rotate %g 0 1 0\n  Scale %g 1 1\n  Scale %g %g %g\n  ObjectInstance "ball"\nAttributeEnd' % (
                material, self.SHAPES[self.shape], f(at), self.rot, -1.0 if self.mirror else 1.0, s, s, s)
        if self.shape != "sphere":   # another shape, turned and mirrored with the scene (its own axes go with it)
            return 'AttributeBegin\n  Translate %s\n  Rotate %g 0 1 0\n  Scale %g 1 1\n  Scale %g %g %g\n  %s\n  %s\nAttributeEnd' % (
                f(at), self.rot, -1.0 if self.mirror else 1.0, s, s, s, material, self.SHAPES[self.shape])
        if self.ball == "scaled":      # the same ball as a radius 2 sphere shrunk by the transform
            return 'AttributeBegin\n  Translate %s\n  Scale 0.5 0.5 0.5\n  %s\n  Shape "sphere" "float radius" [ %g ]\nAttributeEnd' % (f(at), material, 2.0 * s)
        if self.ball == "rotated":     # a sphere does not care how it is turned
            return 'AttributeBegin\n  Translate %s\n  Rotate 37 1 2 3\n  %s\n  Shape "sphere" "float radius" [ %g ]\nAttributeEnd' % (f(at), material, s)
        if self.ball == "explicit":    # every parameter written out at its default
            return 'AttributeBegin\n  Translate %s\n  %s\n  Shape "sphere" "float radius" [ %g ] "float zmin" [ %g ] "float zmax" [ %g ] "float phimax" [ 360 ]\nAttributeEnd' % (f(at), material, s, -s, s)
        if self.ball == "instance":    # the ball as an object instance
            return 'ObjectBegin "ball"\n  %s\n  Shape "sphere" "float radius" [ %g ]\nObjectEnd\nAttributeBegin\n  Translate %s\n  ObjectInstance "ball"\nAttributeEnd' % (material, s, f(at))
        return 'AttributeBegin\n  Translate %s\n  %s\n  Shape "sphere" "float radius" [ %g ]\nAttributeEnd' % (f(at), material, s)

    def text(self):
        p, f = self.p, self.f
        eye, look = (p(0, 0, 5) if self.fill else p(0.5, 1.5, 6.0)), (p(0, 0, 0) if self.fill else p(0.2, 0.6, 0))
        fov = 10 if self.fill else 38
        camera_line = 'Camera "perspective" "float fov" [ %g ]' % fov
        dist = math.sqrt(sum((a - b) ** 2 for a, b in zip(eye, look)))
        if self.camera == "tinylens":
            camera_line = 'Camera "perspective" "float fov" [ %g ] "float lensradius" [ %g ] "float focaldistance" [ %g ]' % (fov, 1e-4 * self.scale, dist)
        elif self.camera == "lens":   # defocus moves energy around, not out of the picture
            camera_line = 'Camera "perspective" "float fov" [ %g ] "float lensradius" [ %g ] "float focaldistance" [ %g ]' % (fov, 0.15 * self.scale, dist)
        elif self.camera == "spherical":   # sees every direction from the eye
            camera_line = 'Camera "spherical"'
        elif self.camera in ("far", "ortho"):   # 200 times as far away, looking at the same place: parallel rays, a window 4 across each way at the look point
            k = 200.0 * self.scale / dist
            eye = tuple(l + (e - l) * k for e, l in zip(eye, look))
            half = 4.0 * self.scale
            if self.camera == "far":
                camera_line = 'Camera "perspective" "float fov" [ %g ]' % (2 * math.degrees(math.atan(half / (200.0 * self.scale))))
            else:
                camera_line = 'Camera "orthographic" "float screenwindow" [ %g %g %g %g ]' % (-half, half, -half, half)
        up = self.p(0, 1, 0)
        up = tuple(a - b for a, b in zip(up, self.p(0, 0, 0)))   # the turned, scaled up vector's direction
        out = ["LookAt %s  %s  %s" % (f(eye), f(look), f(tuple(c / self.scale for c in up))), camera_line,
               'Film "rgb" "integer xresolution" [ %d ] "integer yresolution" [ %d ] %s' % (self.size, self.size, self.film_extra),
               'Sampler "%s" "integer pixelsamples" [ %d ]' % (self.sampler, self.spp), 'Integrator "%s" "integer maxdepth" [ %d ]' % (self.integrator, self.depth), "WorldBegin"]
        out.append(self.sky_text())
        if not self.fill:
            out.append(self.floor_text())
            out.append(self.lamp_text())
        material = self.material_text or MATERIALS[self.material]
        if callable(material):
            prelude = medium_prelude(self)
            if prelude:
                out.append(prelude)
            material = material(self)
        if self.prelude:
            out.append(self.prelude)
        out.append(self.ball_text(material))
        return "\n".join(out) + "\n"


def write_png(path, w, h, pixel):
    """An 8-bit RGB PNG; pixel(x, y) -> (r, g, b) bytes."""
    raw = b"".join(b"\0" + b"".join(bytes(pixel(x, y)) for x in range(w)) for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class Sweep:
    def __init__(self, exe, backends, workdir, verbose):
        self.exe, self.backends, self.dir, self.verbose = exe, backends, workdir, verbose
        self.white = os.path.join(workdir, "white.png").replace("\\", "/")   # forward slashes: a backslash inside a pbrt string is an escape
        write_png(self.white, 64, 32, lambda x, y: (255, 255, 255))
        self.gradient = os.path.join(workdir, "gradient.png").replace("\\", "/")   # brighter to one side: a picture sky that is not the same in every direction
        write_png(self.gradient, 64, 64, lambda x, y: (30 + 3 * x, 40 + 2 * x + y, 255 - 3 * x))
        self.flat_normal = os.path.join(workdir, "flat_normal.png").replace("\\", "/")   # a normal map that points straight out everywhere
        write_png(self.flat_normal, 8, 8, lambda x, y: (128, 128, 255))
        self.sun = os.path.join(workdir, "sun.png").replace("\\", "/")   # a dark sky with one small bright spot (an 8-bit stand-in for the sun): a sharp feature for the sky sampling
        write_png(self.sun, 64, 64, lambda x, y: (255, 255, 230) if (x - 20) ** 2 + (y - 18) ** 2 < 5 else (20, 25, 40))
        self.grey = os.path.join(workdir, "grey.png").replace("\\", "/")   # a uniform image for the texture pair, written with a linear encoding
        write_png(self.grey, 8, 8, lambda x, y: (153, 102, 77))
        # A picture with something in every corner, and the same picture tiled twice across and up: a lookup through uscale/vscale 2 of the first must be the second (texture placement)
        self.pattern = os.path.join(workdir, "pattern.png").replace("\\", "/")
        pattern_pixel = lambda x, y: (40 + 25 * (x % 8), 220 - 22 * (y % 8), 60 + 45 * ((x + y) % 4))
        write_png(self.pattern, 8, 8, pattern_pixel)
        self.pattern_tiled = os.path.join(workdir, "pattern_tiled.png").replace("\\", "/")
        write_png(self.pattern_tiled, 16, 16, lambda x, y: pattern_pixel(x % 8, y % 8))
        self.height = os.path.join(workdir, "height.png").replace("\\", "/")   # a grey picture, so it is a height map
        height_pixel = lambda x, y: ((25 * (x % 8) + 8 * (y % 8),) * 3)
        write_png(self.height, 8, 8, height_pixel)
        self.height_tiled = os.path.join(workdir, "height_tiled.png").replace("\\", "/")
        write_png(self.height_tiled, 16, 16, lambda x, y: height_pixel(x % 8, y % 8))
        self.n = 0
        self.cache = {}

    def render(self, backend, scene, seed=3):
        key = (backend, scene.text(), scene.size, scene.spp, seed)
        if key in self.cache:
            return self.cache[key]
        self.n += 1
        pbrt = os.path.join(self.dir, "s%d.pbrt" % self.n)
        out = os.path.join(self.dir, "s%d.exr" % self.n)
        open(pbrt, "w").write(scene.text())
        try:
            r = subprocess.run([self.exe, "--" + backend, "--seed", str(seed), "--output", out, str(scene.size), str(scene.spp), str(scene.depth), pbrt], capture_output=True, text=True, cwd=REPO, timeout=1200)
        except subprocess.TimeoutExpired:
            raise RuntimeError("%s render did not finish in 20 minutes" % backend)
        if r.returncode != 0 or not os.path.exists(out):
            raise RuntimeError("%s render failed: %s" % (backend, (r.stdout + r.stderr)[-300:]))
        means = read_exr_means(out)
        # Two black pictures are "equal": a lamp that did not light, or a shape that was not drawn, must not pass as 0% off. Every scene here has a sky or a lamp.
        if max(means) < 1e-5:
            raise RuntimeError("%s drew a black picture" % backend)
        self.cache[key] = means
        return self.cache[key]

    def compare(self, label, backend, a, b, tolerance, seed_b=3):
        ma, mb = self.render(backend, a), self.render(backend, b, seed_b)
        worst = max(abs(x - y) / max(abs(x), 1e-6) for x, y in zip(ma, mb))
        ok = worst * 100.0 <= tolerance
        if not ok or self.verbose:
            print("  %-4s %-34s %s  %.4f vs %.4f  off by %.1f%%" % (backend, label, "ok   " if ok else "WRONG", ma[1], mb[1], worst * 100.0))
        return ok


def run(args):
    workdir = tempfile.mkdtemp(prefix="consistency_sweep.")
    sw = Sweep(args.exe, args.backends, workdir, args.verbose)
    only = set(args.only.split(",")) if args.only else None
    tol = args.tolerance
    failures = []
    total = 0

    def noise_tol(name):
        return max(tol, 8.0) if name in NOISY else tol

    def check(group, label, backend, a, b, tolerance=None, seed_b=3):
        nonlocal total
        if only and group not in only:
            return
        total += 1
        try:
            if not sw.compare(label, backend, a, b, tolerance if tolerance is not None else tol, seed_b):
                failures.append((group, backend, label))
        except RuntimeError as e:
            print("  %-4s %-34s FAILED TO RENDER: %s" % (backend, label, e))
            failures.append((group, backend, label))

    for backend in args.backends:
        print("[%s]" % backend)
        for name in MATERIALS:
            # sky: a constant against the same sky as a picture (a ball that fills the frame, so the whole picture is the material)
            check("sky", "sky constant vs picture, %s" % name, backend,
                  Scene(material=name, sky="constant", fill=True, size=32, spp=64, depth=30), Scene(material=name, sky="picture", sky_png=sw.white, fill=True, size=32, spp=64, depth=30), tolerance=noise_tol(name))
            base = Scene(material=name)
            check("translate", "translate, %s" % name, backend, base, Scene(material=name, offset=(37.0, -11.0, 5.0)))
            check("scale", "scale x8, %s" % name, backend, base, Scene(material=name, scale=8.0))
            check("mirror", "mirror, %s" % name, backend, base, Scene(material=name, mirror=True), tolerance=noise_tol(name))
            check("size", "size 32 vs 64, %s" % name, backend, Scene(material=name, size=32), Scene(material=name, size=64), tolerance=noise_tol(name))
            check("spp", "spp 16 vs 128, %s" % name, backend, Scene(material=name, spp=16, size=64), Scene(material=name, spp=128, size=64), tolerance=max(noise_tol(name), 6.0))
            if name not in SPECULAR:
                check("lightform", "point vs sphere lamp, %s" % name, backend, Scene(material=name, lamp="point"), Scene(material=name, lamp="area"), tolerance=max(tol, 6.0))
            if name in ROTATE_MATERIALS:
                # a picture sky that is not the same in every direction, turned together with everything else in the scene
                check("skyrotate", "scene and sky turned 90 deg, %s" % name, backend,
                      Scene(material=name, sky="picture", sky_png=sw.gradient), Scene(material=name, sky="picture", sky_png=sw.gradient, rot=90.0), tolerance=noise_tol(name))
                check("skyrotate", "scene and sky turned 200 deg, %s" % name, backend,
                      Scene(material=name, sky="picture", sky_png=sw.gradient), Scene(material=name, sky="picture", sky_png=sw.gradient, rot=200.0), tolerance=noise_tol(name))
            if name in SHAPE_MATERIALS:
                for ball in ("scaled", "rotated", "explicit", "instance"):
                    check("shapeform", "ball written as %s, %s" % (ball, name), backend, base, Scene(material=name, ball=ball), tolerance=noise_tol(name))
                for floor in ("tri2", "bilinear", "bilinear_plain"):
                    check("shapeform", "floor written as %s, %s" % (floor, name), backend, base, Scene(material=name, floor=floor), tolerance=noise_tol(name))
            if name in LIGHT_MATERIALS:
                check("lights", "spot vs point, %s" % name, backend, Scene(material=name, lamp="point"), Scene(material=name, lamp="spot"), tolerance=max(tol, 6.0))
                check("lights", "distant vs far point, %s" % name, backend, Scene(material=name, lamp="distant"), Scene(material=name, lamp="far"), tolerance=max(tol, 6.0))

        # textures: a value written inline, as a constant texture, as an image, as a checkerboard of two equal colours, as a mix of equal colours
        diffuse_inline = 'Material "diffuse" "rgb reflectance" [ 0.6 0.4 0.3 ]'
        textures = {
            "constant": 'Texture "t" "spectrum" "constant" "rgb value" [ 0.6 0.4 0.3 ]',
            "image": 'Texture "t" "spectrum" "imagemap" "string filename" [ "%s" ] "string encoding" [ "linear" ]' % sw.grey,
            "checkerboard": 'Texture "t" "spectrum" "checkerboard" "rgb tex1" [ 0.6 0.4 0.3 ] "rgb tex2" [ 0.6 0.4 0.3 ] "float uscale" [ 6 ] "float vscale" [ 6 ]',
            "scaled": 'Texture "t" "spectrum" "scale" "rgb tex" [ 0.3 0.2 0.15 ] "float scale" [ 2 ]',
            "mix": 'Texture "t" "spectrum" "mix" "rgb tex1" [ 0.6 0.4 0.3 ] "rgb tex2" [ 0.6 0.4 0.3 ] "float amount" [ 0.3 ]',
        }
        for tname, decl in textures.items():
            check("texture", "diffuse reflectance as %s texture" % tname, backend, Scene(material_text=diffuse_inline), Scene(material_text='Material "diffuse" "texture reflectance" [ "t" ]', prelude=decl), tolerance=tol)
        roughness_tex = 'Texture "r" "float" "constant" "float value" [ 0.1 ]'
        check("texture", "conductor roughness as a texture", backend, Scene(material="conductor"),
              Scene(material_text='Material "conductor" "rgb reflectance" [ 0.9 0.6 0.4 ] "texture roughness" [ "r" ]', prelude=roughness_tex), tolerance=tol)
        check("texture", "coateddiffuse roughness as a texture", backend, Scene(material="coateddiffuse"),
              Scene(material_text='Material "coateddiffuse" "rgb reflectance" [ 0.5 0.3 0.2 ] "texture roughness" [ "r" ]', prelude=roughness_tex), tolerance=tol)
        check("texture", "coateddiffuse reflectance as a texture", backend, Scene(material="coateddiffuse"),
              Scene(material_text='Material "coateddiffuse" "texture reflectance" [ "t" ] "float roughness" [ 0.1 ]', prelude='Texture "t" "spectrum" "constant" "rgb value" [ 0.5 0.3 0.2 ]'), tolerance=tol)
        # texture placement: a picture looked up through the Texture's own uscale/vscale (the renderers' UVMapping, which is how a sphere, disk, cylinder, cone or a mesh file gets the Scene
        # Builder's texture repeats) is the same picture tiled by hand. Every shape that makes its own texture coordinates; on a Mac this is the Metal renderer's job too.
        repeat_decl = 'Texture "t" "spectrum" "imagemap" "string filename" [ "%s" ] "string wrap" [ "repeat" ]%s'
        for shape in ("sphere", "disk", "cylinder"):
            check("texturemap", "picture with uscale/vscale 2 = picture tiled by hand, %s" % shape, backend,
                  Scene(material_text='Material "diffuse" "texture reflectance" [ "t" ]', prelude=repeat_decl % (sw.pattern_tiled, ""), shape=shape),
                  Scene(material_text='Material "diffuse" "texture reflectance" [ "t" ]', prelude=repeat_decl % (sw.pattern, ' "float uscale" [ 2 ] "float vscale" [ 2 ]'), shape=shape),
                  tolerance=1.0)   # tight: Metal without the mapping was 5.1% off on the sphere
        # the same for a bump map: a height picture looked up through uscale/vscale 2 is the picture tiled by hand (the slope is per unit of the surface's own coordinates)
        bump_decl = ('Texture "h" "float" "imagemap" "string filename" [ "%s" ] "string wrap" [ "repeat" ] "string encoding" [ "linear" ]%s\n'
                     'Texture "b" "float" "scale" "texture tex" [ "h" ] "float scale" [ 1.5 ]')
        bump_material = 'Material "diffuse" "rgb reflectance" [ 0.7 0.7 0.7 ] "texture displacement" [ "b" ]'
        check("texturemap", "bump map with uscale/vscale 2 = bump map tiled by hand", backend,
              Scene(material_text=bump_material, prelude=bump_decl % (sw.height_tiled, ""), shape="uvquad"),
              Scene(material_text=bump_material, prelude=bump_decl % (sw.height, ' "float uscale" [ 2 ] "float vscale" [ 2 ]'), shape="uvquad"), tolerance=1.0)   # tight: a bump map looked up without the mapping is only 1.4% (CPU) to 3.6% (Metal) off
        # bump and normal maps that change nothing: a constant displacement, and a flat normal map
        check("texture", "constant displacement texture changes nothing", backend, Scene(material_text=diffuse_inline),
              Scene(material_text='Material "diffuse" "rgb reflectance" [ 0.6 0.4 0.3 ] "texture displacement" [ "d" ]', prelude='Texture "d" "float" "constant" "float value" [ 0.3 ]'), tolerance=tol)
        check("texture", "flat normal map changes nothing", backend, Scene(material_text=diffuse_inline),
              Scene(material_text='Material "diffuse" "rgb reflectance" [ 0.6 0.4 0.3 ] "string normalmap" [ "%s" ]' % sw.flat_normal), tolerance=tol)
        # the sky's scale: a constant sky at 0.4 against a white picture at scale 0.4
        check("skyscale", "constant sky at 0.4 vs white picture at scale 0.4", backend, Scene(material="diffuse", sky="constant", sky_scale=0.4, fill=True),
              Scene(material="diffuse", sky="picture", sky_png=sw.white, sky_scale=0.4, fill=True), tolerance=tol)
        # shapes other than the sphere, each moved, scaled, mirrored and turned
        for shape in SHAPE_LIST:
            base = Scene(material="diffuse", shape=shape)
            for label, other in (("moved", Scene(material="diffuse", shape=shape, offset=(37.0, -11.0, 5.0))), ("8x bigger", Scene(material="diffuse", shape=shape, scale=8.0)),
                                 ("mirrored", Scene(material="diffuse", shape=shape, mirror=True)), ("turned 90", Scene(material="diffuse", shape=shape, rot=90.0)),
                                 ("turned 200", Scene(material="diffuse", shape=shape, rot=200.0))):
                check("shapetransform", "%s %s" % (shape, label), backend, base, other, tolerance=max(tol, 5.0))
            check("shapeform", "%s written as an object instance" % shape, backend, base, Scene(material="diffuse", shape=shape, ball="instance"), tolerance=max(tol, 5.0))
            check("shapeform", "%s as an instance, turned 200" % shape, backend, base, Scene(material="diffuse", shape=shape, ball="instance", rot=200.0), tolerance=max(tol, 5.0))
        # cameras: a lens of almost no radius against a pinhole, a real lens (defocus keeps the energy), and a far perspective camera against an orthographic one
        for name in CAMERA_MATERIALS:
            check("camera", "pinhole vs tiny lens, %s" % name, backend, Scene(material=name), Scene(material=name, camera="tinylens"), tolerance=noise_tol(name))
            check("camera", "pinhole vs lens radius 0.15, %s" % name, backend, Scene(material=name), Scene(material=name, camera="lens"), tolerance=max(noise_tol(name), 6.0))
            check("camera", "far perspective vs orthographic, %s" % name, backend, Scene(material=name, camera="far"), Scene(material=name, camera="ortho"), tolerance=max(noise_tol(name), 6.0))
        # the sampler, the integrator and the film's exposure: the same picture however it is asked for
        for name in ("diffuse", "coateddiffuse", "conductor"):
            for sampler in ("independent", "sobol", "zsobol", "pmj02bn", "stratified", "paddedsobol"):
                check("sampler", "%s sampler vs halton, %s" % (sampler, name), backend, Scene(material=name), Scene(material=name, sampler=sampler), tolerance=max(tol, 6.0))
            check("integrator", "path vs volpath, %s" % name, backend, Scene(material=name), Scene(material=name, integrator="path"), tolerance=max(tol, 6.0))
        check("film", "iso 200 for 1 s vs iso 100 for 2 s", backend, Scene(material="diffuse", film_extra='"float iso" [ 200 ] "float exposuretime" [ 1 ]'),
              Scene(material="diffuse", film_extra='"float iso" [ 100 ] "float exposuretime" [ 2 ]'), tolerance=tol)
        # a camera that sees every direction, the scene moved, scaled, turned and mirrored. Without the lamp: it would show in a few pixels at a radiance of 318, and where
        # those pixels land differs between samplers by up to 4% of the picture's mean (the Windows session's wavefront backend and Metal both hit this).
        sph = dict(material="diffuse", camera="spherical", lamp="none")
        for label, other in (("moved", Scene(offset=(37.0, -11.0, 5.0), **sph)), ("8x bigger", Scene(scale=8.0, **sph)), ("turned 90", Scene(rot=90.0, **sph)), ("mirrored", Scene(mirror=True, **sph))):
            check("camera", "spherical camera, scene %s" % label, backend, Scene(**sph), other, tolerance=max(tol, 5.0))
        # a sky with a small bright spot, lighting the scene alone, turned with the scene: the sampling of a sharp feature must agree with the lookup of the picture
        for name in SUN_MATERIALS:
            for angle in (90.0, 200.0):
                check("sun", "sun sky and scene turned %g deg, %s" % (angle, name), backend, Scene(material=name, sky="picture", sky_png=sw.sun, lamp="none", spp=128),
                      Scene(material=name, sky="picture", sky_png=sw.sun, lamp="none", rot=angle, spp=128), tolerance=max(noise_tol(name), 8.0))
        # a mix of two named materials: mixing a material with itself, or at amount 0 or 1, is one of them
        named = ('MakeNamedMaterial "ma" "string type" [ "diffuse" ] "rgb reflectance" [ 0.6 0.4 0.3 ]\n'
                 'MakeNamedMaterial "mb" "string type" [ "conductor" ] "rgb reflectance" [ 0.9 0.6 0.4 ] "float roughness" [ 0.1 ]')
        plain_a, plain_b = 'NamedMaterial "ma"', 'NamedMaterial "mb"'
        for label, mix, plain in (("a with itself", 'Material "mix" "string materials" [ "ma" "ma" ] "float amount" [ 0.4 ]', plain_a),
                                  ("amount 0 is the first", 'Material "mix" "string materials" [ "ma" "mb" ] "float amount" [ 0 ]', plain_a),
                                  ("amount 1 is the second", 'Material "mix" "string materials" [ "ma" "mb" ] "float amount" [ 1 ]', plain_b)):
            check("mix", "mix, %s" % label, backend, Scene(material_text=plain, prelude=named), Scene(material_text=mix, prelude=named), tolerance=max(tol, 5.0))
        # hair on curves: the same scene moved, scaled and turned, and under the two forms of sky
        hair = 'Material "hair" "rgb sigma_a" [ 0.3 0.5 0.9 ]'
        hbase = Scene(material_text=hair, shape="hair", spp=512)
        for label, other in (("moved", Scene(material_text=hair, shape="hair", spp=512, offset=(37.0, -11.0, 5.0))), ("8x bigger", Scene(material_text=hair, shape="hair", spp=512, scale=8.0)),
                             ("turned 90", Scene(material_text=hair, shape="hair", spp=512, rot=90.0))):
            check("hair", "hair curves %s" % label, backend, hbase, other, tolerance=10.0)   # the tube's eight facets do not turn exactly with the scene
        check("hair", "hair curves, constant vs picture sky", backend, Scene(material_text=hair, shape="hair", spp=512, fill=True, size=32), Scene(material_text=hair, shape="hair", spp=512, fill=True, size=32, sky="picture", sky_png=sw.white), tolerance=6.0)
        # more light kinds against a point light
        check("lights", "white-picture area light vs plain, diffuse", backend, Scene(material="diffuse"), Scene(material="diffuse", lamp="texarea", lamp_png=sw.white), tolerance=max(tol, 6.0))
        for lamp in ("gonio", "projection"):
            check("lights", "%s vs point, diffuse" % lamp, backend, Scene(material="diffuse", lamp="point"), Scene(material="diffuse", lamp=lamp), tolerance=max(tol, 6.0))

    # backends: the same scene on the CPU and on the GPU (only when both are asked for)
    if "cpu" in args.backends and "gpu" in args.backends and (not only or "backends" in only):
        print("[cpu vs gpu]")
        for name in MATERIALS:
            spp = 512 if name in NOISY else 64   # a glass ball lit through by a small lamp has caustics: one backend's few fireflies can move a 64-sample mean by 9%
            for label, scene in (("constant sky", Scene(material=name, spp=spp)), ("picture sky", Scene(material=name, sky="picture", sky_png=sw.gradient, spp=spp))):
                total += 1
                try:
                    ma, mb = sw.render("cpu", scene), sw.render("gpu", scene)
                except RuntimeError as e:
                    print("  both cpu vs gpu, %s, %s: FAILED TO RENDER: %s" % (label, name, e))
                    failures.append(("backends", "both", "%s, %s" % (label, name)))
                    continue
                worst = max(abs(x - y) / max(abs(x), 1e-6) for x, y in zip(ma, mb)) * 100.0
                limit = max(noise_tol(name), BACKEND_TOLERANCE.get(name, 6.0))
                if worst > limit or args.verbose:
                    print("  %-4s %-34s %s  %.4f vs %.4f  off by %.1f%%" % ("both", "cpu vs gpu, %s, %s" % (label, name), "ok   " if worst <= limit else "WRONG", ma[1], mb[1], worst))
                if worst > limit:
                    failures.append(("backends", "both", "%s, %s" % (label, name)))
    print("\n%d pairs, %d off by more than %g%%" % (total, len(failures), tol))
    if not args.keep:
        shutil.rmtree(workdir, ignore_errors=True)
    else:
        print("scenes and pictures kept in %s" % workdir)
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=os.path.join(REPO, "build_macos", "ray_tracer"))
    ap.add_argument("--backends", default="cpu,gpu")
    ap.add_argument("--only", default="")
    ap.add_argument("--tolerance", type=float, default=4.0, help="percent, per colour channel")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--keep", action="store_true", help="keep the folder of generated scenes and pictures (removed by default)")
    args = ap.parse_args()
    args.backends = [b for b in args.backends.split(",") if b]
    if not os.path.exists(args.exe):
        sys.exit("ERROR: %s not found - build it first" % args.exe)
    sys.exit(run(args))


if __name__ == "__main__":
    main()
