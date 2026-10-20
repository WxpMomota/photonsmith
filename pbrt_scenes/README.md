# Custom scenes

Any `.pbrt` file placed directly in this folder appears in the renderer as a
scene, in the CLI (by its file name: `ray_tracer.exe --cpu 400 64 8 my-scene`, or by
the file's path) and in the GUI's **Custom Scenes** tab, with no rebuild.

Point the renderer at a different folder with the `RAY_TRACER_PBRT_DIR`
environment variable — useful for a scene collection you keep outside this
repository:

```bash
RAY_TRACER_PBRT_DIR=D:/pbrt-v4-scenes/killeroo ./ray_tracer.exe --cpu 400 64 8 killeroo
```

## Saying what a scene is

The bundled test scenes begin with comment lines that tell the renderer about them:

```
# @rt-category Test Scenes
# @rt-description A fog ball that scatters but does not absorb, under a white sky.
# @rt-performance Fast
# @rt-size 6.4
```

`@rt-size` is the scene's size: the largest side of its bounding box in the scene's own units. The Live Preview scales its keyboard step by it. `python scripts/stamp_scene_sizes.py` writes it for a scene you add (see [docs/SCENE_SELECTION.md](../docs/SCENE_SELECTION.md)).

A scene of your own can do the same (`@rt-category` must be one of the categories in
[docs/SCENE_SELECTION.md](../docs/SCENE_SELECTION.md); without one it is a Custom Scene). A file
with no description is listed with its resolution and samples. A scene saved by the Scene
Builder is listed under the title you gave it.

## What is read, and when

Listing a scene reads only the part of the file before `WorldBegin` — the
camera, film and sampler. Geometry, `Include`d files and `.ply` meshes are read
the first time that scene is actually rendered, so a folder of large scenes
costs nothing to browse.

That is also why a scene's reported performance is "Unknown": nothing in a
pbrt header says whether the world behind it holds three triangles or ten
million.

## Names and ids

A scene is named by its file name without the extension, lower-cased with hyphens:
`My Scene.pbrt` is `my-scene`. That name is the scene's durable key: it is what the
command line, the recent-renders list and the thumbnail cache use, and it does not
change when you add or remove other files. A scene from a collection in its own
sub-folder is prefixed with that folder (`killeroo/frame25.pbrt` is `killeroo-frame25`);
if two files would get the same name the later one gets `-2`.

Each scene also has a short id (`K37`), numbered in filename order after the built-in
ones, and the id is accepted anywhere a name is. But adding a file that sorts earlier
shifts the ids of the ones after it, so scripts should use the name.

## Supported subset

The parser covers a large and growing subset of pbrt-v4 — rather than keep an
itemized list in sync here (it has outgrown that once already), the
authoritative list is the directive dispatch in `src/shared/pbrt_scene.h`.
As of this writing that includes: `LookAt`/`Translate`/`Rotate`/`Scale`/
`Transform`, `AttributeBegin`/`End`, `ObjectBegin`/`ObjectInstance`
(instancing), `ReverseOrientation`, `Accelerator`, `CoordinateSystem`,
`ColorSpace`, `Camera`, `Film`, `Sampler`, `Material`, `MakeNamedMaterial`/
`NamedMaterial`, `Texture`, `MakeNamedMedium`/`MediumInterface` (homogeneous,
cloud, nanovdb and rgbgrid media), `LightSource` (point, spot, distant,
infinite, projection, goniometric) and `AreaLightSource`, `Shape`
(`trianglemesh`, `plymesh`, `sphere`, `cylinder`, `cone`, `disk`,
`paraboloid`, `curve`, `bilinearmesh`), animated transforms (motion blur),
and `Include`. The `measured` material (a real, importance-sampled pbrt-v4
`MeasuredBxDF` loaded from a `.bsdf` tensor file) is also supported — see
`pbrt_scenes/measured-brdf-showroom.pbrt` for an example.

Anything the parser does not understand is skipped with a warning on stderr
rather than failing the load, so a scene using an unsupported feature still
renders — without that feature. Unsupported constructs are worth reading the warnings
for: a missing displacement map or medium can change a render substantially.
One known structural gap: there is no `.obj` mesh ingestion at all (only
inline `trianglemesh` and external `.ply`), so a scene that references raw
`.obj` files won't load as-is.

## Authoring conventions

Not enforced by the parser, but followed by every scene this project authors
itself (as opposed to a downloaded multi-file bundle like
`barcelona-pavilion/` or `sportscar/`):

- **Licensing header**: a self-authored scene ends its header comment with
  `# Original, not derived from any distributed scene, so it carries no
  licence constraints.` A scene migrated from a native C++ demo scene opens
  with a short provenance note instead (which native scene/function it
  replaces, and whether it's a byte-for-byte port or a disclosed fidelity
  improvement) — see `scene_registry_data.h`'s matching comment for the same
  scene for the full rationale.
- **Sibling binary assets resolve scene-directory-first**: a bare filename
  in `"string filename" [ "foo.bsdf" ]` (or `.exr`, `.ply`, `.bmp`, `.nvdb`)
  resolves against the `.pbrt` file's own directory first, then as given —
  the same rule `Include`d files use for their own nested includes (see
  `src/shared/pbrt_load.h`'s `resolveExistingPath()`). This is why every
  scene here references its sibling assets by bare filename with no path.

## CPU and GPU

These scenes render on both backends, from the same parsed scene — pass
`--gpu` or `--cpu`.

The GPU is the weaker of the two here, in one specific way: it can only sample
area lights that are spheres or **parallelograms**. pbrt writes area lights as
triangle meshes, and the loader rejoins triangle pairs into quads to cover the
usual case, but a light that is a general quadrilateral, a triangle fan, or a
genuine single triangle cannot be converted. Those still emit when a ray hits
them directly, so the image is darker and noisier rather than wrong — and the
GPU build prints exactly how many lights it could not convert. If you see that
warning, use `--cpu` for that scene.

## Getting scenes

The pbrt-v4 scene collection lives at
<https://github.com/mmp/pbrt-v4-scenes>. Individual scenes there carry their
own licences — several are non-commercial or no-derivatives, so check before
redistributing anything you download.

**Benedikt Bitterli's rendering resources** (<https://benedikt-bitterli.me/resources/>) are a set of well-known, beautiful scenes (living rooms, a kitchen, a bedroom, staircases, a glass of water, cars,
a lamp, a spaceship, a Japanese classroom...), each released under CC0 or CC BY 3.0 (the author's credit travels in the scene's own `LICENSE.txt`). They are in the scene list as *Large Scenes*
(Grey and White Room, Bedroom Interior, Country Kitchen, Modern Hall, ...) and are not stored here: select one and press **Download missing files**, and its pbrt-v4 version is fetched from
benedikt-bitterli.me into `pbrt_scenes/bitterli/<scene>/` (5 to 50 MB each; only what the scene uses, every file checked). Restart the app once after the first download so the scene is read with its
own camera. They are the scenes' original files, unchanged: `scene-v4.pbrt`, `models/`, `textures/`.

**pbrt-v4-scenes: Kroken, Watercolor and Head** (checked 2026-10-20 against the renderers):
- **Kroken** and **Watercolor** (interiors licensed from Angelo Ferretti, CC BY-ND 2.0) are in the scene list (H43, H44; 3.1 GB and 3.0 GB to download, the files unchanged; `texture-means`, `portal-light-rotated` and `portal-light-turned` are in the reference pack). They draw as pbrt does to within about 10% of the picture's mean brightness. What the loader does for them: a portal infinite light's corners are world-space points and its picture is turned by the light's transform (they were carried through it, so the window was in the wrong place and the room got no sky); a portal light given only a colour (`blackbody L`) is drawn as pbrt does; a file with several portal infinite lights keeps them all and sums them (Watercolor has one per window; a plain sky next to portals is dropped, with a warning); and a texture bound to a parameter the renderers cannot evaluate per hit (a roughness map, a `mix` amount, a conductor's colour image, a `scale` of two textures) takes the texture's average value instead of the parameter's default, so a roughness map is no longer a mirror. What is still not per pixel: those averaged textures (dirt and paint overlays, book covers), the coated conductor's `albedo`/`g`/`maxdepth`, and a plymesh with a `MediumInterface`.
- **Head** (a subsurface head, CC BY 2.0, 53 MB) is the scene H45. Its file's film is 1920 x 1080 with a crop window on the face, and the picture is that window, as pbrt writes it: the renderers draw only the window and the launcher cuts the finished image to it (`SceneDescriptor::crop_is_picture`, `src/shared/output_crop.h`; an explicit `--crop` keeps the black-surrounded window, as it always has).
