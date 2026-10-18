# From a picture to a scene

Four ways to get a real-world picture into a rendered scene, from simple to ambitious. The first three are in the
[Scene Builder](SCENE_BUILDER.md); the fourth is a workflow using free tools, with the renderer at the end of it.

| You have | You want | Use |
|---|---|---|
| any picture | it on a wall, a floor, a ball, a statue | **Pictures** (built in, nothing to install) |
| a photo of a **surface** (wood, tile, fabric, a wall) | a material that repeats over any object, with a bump map | **Material from a photo** (built in, nothing to install) |
| a photo of an **outdoor scene** with sky | the sun's height and direction and the haze, to light a render the same way | **Light from a photo** (built in, nothing to install) |
| a photo with a **horizon** | the camera's lens, tilt and roll and the picture's shape, so a scene is seen the way the photo was | **Camera from a photo** (built in, nothing to install) |
| the same photo, with a scene matched to it | the scene's objects in the photo, with their shadows on its ground | **Render into the photo** (built in, nothing to install) |
| **one** photo of an object | a 3D version of it to light and render | **Object from a photo** (optional helper, runs on your computer) |
| **one** photo of a room | an editable room: walls, floor, ceiling, windows, and the camera | **Room from a photo** (the same helper) |
| **many** photos of an object or place | an accurate 3D copy | **A photo scan** with COLMAP or Meshroom, then bring the mesh in |

## Pictures

A diffuse or glossy-paint material can be coloured by a picture instead of a flat colour: pick the material's
**Picture** in the Scene Builder's properties (**Clear** goes back to a colour). PNG, JPEG, BMP, TGA, EXR and HDR work.

- On a **quad**, choosing a picture also gives the quad the picture's shape, and the quad is written so the picture is the
  right way up and not mirrored when you stand the quad up as a wall (rotate it 90 degrees about X; it then faces +Z, towards
  the default camera).
- On a **sphere**, the picture wraps around it (its poles are on the Z axis, so with the default camera you look at a pole).
- On a **mesh**, the mesh's own texture coordinates decide where the picture goes. A mesh without them shows one colour.
- The picture is read as an ordinary (sRGB) picture, so what you see is the colour you put in.
- The scene file stores the picture's path. Keep the picture where it is, or choose it again after moving the scene. The Scene Builder warns when a
  picture, mesh or sky file it points to is no longer there (the renderer would otherwise just render without it).
- A phone photo is often stored sideways with a rotation tag that the renderer ignores. When a picture has one, the Scene Builder saves an upright PNG
  copy under its data folder (`pictures\`) and uses that, so the picture is the right way up and a quad takes the right shape.

For a sky, give the **Sky** light an image instead (an equirectangular panorama).

## Material from a photo

A photo of a real surface (a wood floor, a tiled wall, fabric, a plastered ceiling) can become a material that covers any object. Select an object whose material is **Matte** or
**Glossy paint**, and under **Picture** press **Make from a photo...**. Nothing has to be installed: it is plain image processing, the same on every system, and the photo stays on
your computer.

1. Choose the photo. The middle of it (a square a little inside its edges) is used, so frame a patch of the surface that fills the picture and holds nothing else (no furniture, no
   edge of the floor).
2. The dialog shows what you will get: the picture **repeated twice across and down** (any joins would show here) and the **bump map** (light is high).
3. Say **what it is** (wood, tile or polished stone, brick or rough stone or concrete, fabric or carpet, leather, plaster or paint, something else). That sets the starting gloss (a
   varnished floor, a glazed tile and leather get a clear coat, so the material becomes Glossy paint; the others stay Matte) and how tall the relief is (a millimetre or two for wood
   grain, four for brick). They are starting values; every one can be changed in the properties.
4. Say **how wide the patch in the photo really is**. The picture then repeats across the object at that size: a 40 cm patch on a 6 m floor repeats 15 times. (On a quad or a box;
   other shapes keep their own texture repeats, and 0 leaves the repeats as they were.)

What it makes, from the photo:

- **A colour picture with the photo's light taken out.** A window's glow across a floor, or a shadow, is not part of the wood: the broad light and shade is divided out (the colour is
  kept), so the surface is as bright on average everywhere and takes the light of your scene instead.
- **A picture that repeats with no joins.** The patch is laid over a copy of itself shifted by half its size: the picture's edges must come from the shifted copy (which is continuous
  across the wrap) and its middle from the photo, and the line where one gives way to the other is cut along the path where the two nearly agree (found for the across and then the down
  direction), a few pixels wide, with the difference in brightness at the cut spread into the copy. So planks, grain and grout stay sharp, with no ghosting. If the two agree nowhere better
  than anywhere else (a strong ramp of light that is not an even texture) there is no path to hide a cut in, and the two are blended instead, which is softer. Either way it works best on
  an even texture (wood, stone, plaster, fabric); a patch with a single distinct thing in it (a knot, a stain) repeats that thing, and a large regular feature (a tile with its grout) repeats
  as the grid it is.
- **A bump map guessed from the picture.** Dark is low and light is high at two scales (the fine grain and the coarse pattern), so grout, cracks and the gaps between threads dip. It is a
  guess from the colours alone, not a measurement of the surface: **Bump height** in the properties makes it stronger or flatter, and a surface whose colour does not follow its relief (a
  flat painted wall with a pattern printed on it) gets a relief it does not have.

Limits: the gloss is a starting value by kind, not measured from the photo (a photo does not tell a coat's roughness); a surface with strong perspective in the photo (a floor seen at a
steep angle) is read as if it were flat-on, so stand above the surface when you can; the pictures are 256, 512 or 1024 pixels square by the photo's size and are kept with the Builder's
other pictures, not inside the scene file.

## Light from a photo

A photo of an outdoor scene can set the Builder's **Sun & sky** so that a rendered object is lit as the photographed scene was. Tick **Sun & sky** in a sky light's properties and press
**Match to a photo...** (under Sky gallery). Nothing has to be installed: it is plain image analysis, the same on every system, and the photo stays on your computer.

The dialog asks for two things, both optional: the photo's **field of view** (65 degrees is a phone's main camera; a wide-angle lens is 100 or more) and the **camera tilt** (left at "from
the horizon" it is read off where the sky ends). It then sets the sun's **height**, its **direction** and the **haze** and tells you how it read the photo; one Undo puts the sky back.

How it reads the photo:

- **The sky** is the blue or white region that hangs from the top of the picture, column by column, down to where something else (trees, a building, the ground) begins, or where it gets much
  darker (a blue sea below the horizon is not sky).
- **The tilt of the camera** comes from the lower edge of the sky, taken to be the horizon. If the horizon is not in the photo, or hidden by buildings or trees, the camera is taken to look
  level and the tilt can be given.
- **The sun**, if it is in the picture (one clearly dominant, nearly white spot that floats inside the sky, touching neither the picture's edge nor the skyline, with a glow of the same
  brightness all round it), is read straight off it: height and direction to within a few degrees. White cloud tops are bright too, but there are several, they run into the sky's edge or are
  lumpy, so they are not taken for the sun; a very large glare near the edge is left to the fit, which reads it just as well.
- **If the sun is out of the picture**, the Hosek-Wilkie sky (the model the Builder renders its sky with) is fitted to how bright the sky is in each direction: for every sun height, direction
  and haze it predicts the sky's brightness everywhere, and the sky that follows the photo's brightness best wins (up to one overall gain, since the photo's exposure is not known). A sky is
  brighter towards the sun and near the horizon, and that is what is fitted. The result says how much of the sky's variation the fit explains.
- **The direction** is set relative to where the Builder's camera looks now, as the sun was relative to where the photo looks: a sun 20 degrees to the right of the photo's centre is 20
  degrees to the right of the Builder camera's view.

Checked on 64 skies made from the same model with known suns (heights from 8 to 70 degrees, directions all round, two hazes, a building hiding part of the sky): the mean error is under 5
degrees in height and in direction, with the sun in the picture or outside it. The limits:

- **A low, hazy sun** (about 8 degrees, with haze) turns the sky warm, and the sky finder looks for blue and white: such a sky is cut short and the tilt and height come out wrong.
  **A sunset or sunrise sky is not read**; set those by hand or with the Sky gallery.
- **An overcast sky** has no gradient to read: the sun is set high with the haze at its most, and it says so. A clear blue sky seen high up and away from the sun is even too, but it is blue, not
  grey: it is not called overcast, and its haze is left ordinary.
- **No horizon in the picture** (a camera looking high up): the camera is taken to look level, which makes the sky read hazier than it is; give the tilt when you know it.
- **The field of view** is read from the photo's own EXIF data when it has it; else it is a phone's 65 degrees along the long side of the photo (for a portrait photo the dialog starts at 51, the same lens across its short side).
- **The brightness** stays as it was: the photo's exposure is not known. Use the light's strength.
- **The ground's brightness** is not read (a snow field and a dark ground look alike here).
- **A photo with little sky**, or none, is refused; one with clouds and clear blue mixed may give a rougher fit (not tried).
- **Real photos** (nine free-licence photos from the web, no ground truth for any of them, so judged by eye): a desert with the sun in view and a low sun behind a roof gave the sun's height and
  direction as they look in the picture; a beach and a canyon rim with the sun out of the picture gave plausible, front-lit suns (the scenes look front-lit); a stormy sky, a cloudy meadow
  and an overcast sky with big clouds gave a low confidence and say so; a hazy sunrise (a warm sky) was found as only a sliver of sky and says so; a beach with a thin strip of sky was not
  claimed to be well fitted. Trying them found three faults, now fixed and tested: a blue sea counted as sky, bright cloud tops taken for the sun, and a near-perfect confidence from a thin
  strip of sky. A real photo's own tone curve, lens vignetting and clouds still cost accuracy: check the result by eye and change **Sun height** and **Sun direction** if it is off.

## Camera from a photo

A photo can set the Builder's **camera** so that a scene is seen the way the photo was taken. In the **Camera and image** properties press **Match to a photo...** at the top. Nothing has to be
installed: it is plain image analysis, the same on every system, and the photo stays on your computer.

The camera takes:

- **the lens** (the photo's field of view), read from the photo's own EXIF data when it has it (the 35 mm equivalent focal length a phone or camera writes into a JPEG), else a phone's is assumed
  (65 degrees across the long side, 51 across the short side of a portrait photo). You can also type it;
- **the tilt** (looking up or down) and **the roll** (a tilted horizon), read off the horizon, which is the lower edge of a blue or white sky: a line is fitted through it, its slope is the roll and its
  distance from the middle of the picture is the tilt. You can give either instead;
- **a height** above the ground (1.6 m, eye level, to start with), which only you know; and it **keeps the direction it looked in**: where the camera stands and which way it faces are not in a photo
  (the Builder camera is turned up or down and rolled as the photo's was, along the heading it has).

The **picture takes the photo's shape** (the width stays, the height follows), the 3D view looks **through the camera**, and the photo is laid over it at half opacity so a scene can be lined up against it;
**Show the photo over the camera view**, **Photo opacity** and **Remove the photo** are in the Camera section. A tick box sets the **Sun & sky from the same photo** (see Light from a photo).

How good it is:

- Checked on photos made from a camera of known tilt and roll (to within a degree), and by rendering a scene through the camera it makes (a huge ground under a plain sky): the horizon in the render lands where
  the photo's did, with the signs of the tilt and the roll right all the way through the Builder's document, the pbrt file and the renderer.
- Tried on nine free-licence photos from the web (no ground truth): the clean horizons (two beaches, a heath) gave plausible tilt and roll and said nothing; hills, dunes, a roof edge and a skyline of far
  hills, where the sky's lower edge is not a clean straight line, gave a warning ("check them by eye, or give them"), and a photo with only a sliver of sky said so.
- **A horizon hidden by buildings or trees along the whole of it** is read as the skyline, which is not level: the camera then comes out tilted or rolled wrongly. The warning catches a ragged edge, not a clean
  roof line, so check the tilt and roll by eye against the photo laid over the view.
- The renderers draw the scene on its own (the photo is laid over the view in the Builder, to line things up); **Render into the photo...** (below) puts the scene into the photo with its shadows. Light from a photo sets the light to match.

## Sample photos

Camera from a photo and Light from a photo are **one window**: at the top the photo to use, with **five sample photos built into the program** (a picture of each, what it is good for, and its credit) so you can try them without a photo of your own, and **Choose my own photo...** (which adds your photo to the row, selected); below it the settings; and **there is no Ok button**: clicking a photo applies it at once, and changing a setting applies again (after a short pause, so typing a number is one apply); the result appears **under the settings** in that same window (which opens large, most of the screen's height). Each apply takes the last one of the window back first, so the window leaves one apply behind (one undo step for the camera, one for the sky), however many photos you tried. A short screen scrolls the settings instead of cutting them off. Close ends it. Start with *Beach, blue sky*: a clean horizon and a clear sky, from which the camera, the sun and the exposure of Render into the photo all read well.

| sample | good for | credit |
|---|---|---|
| Beach, blue sky | everything: a straight horizon, a clear sky | Dietmar Rabich, "Juist, Strand -- 2014 -- 3658", CC BY-SA 4.0 |
| North Sea beach | a second straight horizon | Dietmar Rabich, "Norderney, Nordstrand -- 2016 -- 5172", CC BY-SA 4.0 |
| Heath under clouds | a cloudy sky: the objects' exposure is left to you | Dietmar Rabich, "Vreden, Zwillbrocker Venn -- 2016 -- 4122", CC BY-SA 4.0 |
| Meadow, partly cloudy | hills on the horizon: check the tilt by eye | vastateparksstaff, "Touch the Sky at Sky Meadows", CC BY 2.0 |
| Overcast hills | a grey sky: the light comes out overcast | paul morris oldskool2016, "Overcast sky over Snowdonia", CC0 |

The photos are from Wikimedia Commons, scaled to 1280 pixels across (that is the only change); they are in `qt_gui/sample_photos/` and compiled into the program (`resources.qrc`). Their licences are in
`THIRD_PARTY_NOTICES.md`. After you choose one for the camera, the message says what to do next: pick an object, place it by clicking the ground, and render into the photo.

## Place objects on the photo's ground

With the camera matched to a photo, an object is put where you want it in the photo by clicking: pick the object, then **Place the picked object by clicking...** (in its properties, under Rotation; also in the
Arrange menu as *Place on the ground by clicking...*). The 3D view looks through the camera, the cursor is a cross, and each click on the ground in the picture **stands the picked object there**: the middle of
its base is under the click and its lowest point is on the ground (the plane y = 0, which the camera is 1.6 m above unless you gave another height). Click again to move it; Esc stops. Several picked objects are
placed together, keeping their places relative to each other. A click above the horizon finds no ground and does nothing.

The scene's units mean nothing in a photo, so **Set the picked object's real height...** says how tall the object is in the photo (metres): it is scaled to that, uniformly, about the middle of its base, so it keeps
standing where it is. The camera's height gives the scale of everything else: a person in the photo is about 1.7 m, a door 2 m.

Checked by unit tests (the base lands on the ground, a pick of two keeps its arrangement, a scaled object stays on the ground) and by the headless GUI test, which sends a real mouse click to the view.

## Render into the photo

**Render into the photo...** (in the Camera section, under *Match to a photo...*; it needs the camera to have been matched to a photo first, see Camera from a photo, and says so if not) puts the scene into that
photo: the objects, and the shadows and reflections they cast on the ground the photo already shows. Nothing has to be installed; it uses the renderer you already have.

How it works (a shadow catcher without any change to the renderer): the scene is rendered three times from the matched camera,

1. **as it is**;
2. **without the inserted objects** (the ground and the lights only): what the photo's ground would look like with nothing on it;
3. **as a mask** of the inserted objects (plain white on black, the ground black so that it hides what is behind it).

The ratio of the first two, in linear light, is what the objects do to the ground: a shadow darkens it, the glow or reflection of a bright object lightens it. It multiplies the photo. The objects themselves
are pasted in where the mask says, with the ground's share taken off their edge pixels first (or they would carry a halo of the render's ground). The ratio is smoothed a little and has a small dead-band round 1,
so that the render's noise does not grain the photo; everywhere the objects do nothing, the picture is the photo's own pixels.

In the dialog, say **what each object is** (a list with a choice beside every object): **Put into the photo**; **Ground** (the floor the objects stand on: the photo already has it, so it is not drawn, only the shadows it catches count); or **Light** (a lamp: it lights the scene and is not shown). The first guess is: an emissive object is a light, an object called Floor or Ground (alone, or with a number) or a big flat quad at the bottom of the scene is the ground, and everything else, a mesh made from a photo included, is put into the photo. What you say is remembered by object name for **Preview in the photo**. **Exposure of the objects** brightens or darkens them (a stop doubles or halves their light) to match the photo, whose own exposure is not known; **Samples per
pixel** smooths the shadows. **Denoise the renders** (the same as the Denoise tick next to Preview) runs the AI denoiser on the "as it is" and "without the objects" renders before the objects are put in, so 16 samples look clean (the mask is not denoised: it is nearly noiseless and a denoiser would soften its edge; the two denoised renders get the same kind of smoothing, so the shadows' ratio is not disturbed). It is NVIDIA's denoiser on the GPU and Open Image Denoise on the CPU (the first time, the app offers to download it, as for Preview); when it cannot run, the picture is made without it and the message says why. Measured on the GPU at 8 samples: the grain on the objects fell to a quarter. The picture (at most 1000 pixels across) appears **in the Builder's preview pane**, below the 3D view, where Preview's pictures appear: nothing is saved until you press **Save picture...** (next to Preview), which saves whatever the preview pane shows as a PNG. The scene is rendered three times, so it takes about three times as long as one picture; the Log Output tab shows each render as it runs.

**Preview in the photo** (next to Preview) is the quick form: no questions, the Preview's Quality (Draft, Good or Best), the ground, exposure and matching last used in the dialog (the first time the floor is the ground and the sky's exposure is matched when the scene has a Sun & sky), and the picture appears in the preview pane. Use it while placing objects; use the dialog for the final picture.

What to expect:

- The **ground must be a plane the camera sees as the photo does**: put the floor at the height of the real ground (the camera is 1.6 m above it unless you gave another height), and tick it.
- **Lines up only as well as the camera matches.** If the tilt, roll or lens are a little off, the objects stand on the ground in a slightly wrong place: check with the photo laid over the view first.
- **Light from the same photo** (the tick box in Match to a photo, or Light from a photo) makes the shadows fall the way the photo's do; a shadow in the wrong direction is the most obvious sign of a pasted object.
- The objects are tone mapped the way the renderer shows any picture (ACES filmic); the photo is not touched except under shadows.
- **Match the objects' exposure and colour to the photo's sky** (ticked when the scene has a Sun & sky): the scene's sky, set from the same photo and rendered with the same camera, and the photo's sky are the same sky seen by two
  cameras, so the exposure that makes the render's sky come out as the photo's is the exposure the photo had, and what its sky has too much or too little of in each colour is its colour cast. The objects get both (the cast
  limited to +-20% per colour and brightness-neutral). It is read pixel by pixel from the photo's own sky (as Light from a photo finds it) with the objects left out and clipped pixels ignored; the median is
  used, so trees, a sun disc and a cloud the model does not have are outliers. **When the photo's sky is mostly clouds the scene's sky does not look like it, and the dialog says so and leaves the exposure to you** (the Exposure
  setting is added to the match, or used alone then). Tried on real photos: a clear beach sky matched (+0.1 stops, the objects looked right); an overcast heath photo was declined. Checked with synthetic skies of known
  exposure and cast (within a quarter of a stop), with clouds, trees and objects in the way, and with a sky that is not the photo's. Use Exposure when the objects still look too bright or too dark.
- What it does **not** do: the objects do not catch the photo's colours (a red wall beside them does not tint them), reflective objects reflect the scene's sky and not the photo, and anything in front of the
  objects in the photo (a railing, a branch) stays behind them.
- Checked with synthetic pictures of known answers (the far field is exactly the photo, a halved light is a halved shadow, a half-covered edge is un-matted, noise does not show), by rendering a sphere and
  a sun through the launcher and testing the pixels, and by a headless GUI test of the whole thing.

## Object from a photo

**Add > Object from a photo...** turns one photo into a textured 3D mesh and puts it in the scene, standing on the floor and
about two units tall. A model called [TripoSR](https://github.com/VAST-AI-Research/TripoSR) (MIT licence) guesses the
shape. It runs on your own computer; **the photo is not uploaded anywhere**.

It is a guess, not a scan. Expect:

- the front to look right and the **back to be invented**, so check the object from every side before relying on it;
- soft detail: a face, text or thin parts (a chair's slats, a plant) come out smooth or blobby;
- the colour to be baked into the surface, so highlights and shadows in the photo stay in the texture. Light the object
  evenly (soft light, no hard shadows) and it re-lights well; a photo with strong sun on one side keeps that side bright.
- one object per photo, on a plain background if you can. The background is removed automatically (a PNG that already has
  a transparent background is used as it is).

### Setting it up (once)

The helper is **optional**: it needs PyTorch and about 5 GB of disk, so it is not part of the program. From the program's
folder, in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_photo_to_mesh.ps1
```

This needs Python 3.10, 3.11 or 3.12 and git on your PATH. It makes a private Python environment in
`%LOCALAPPDATA%\RayTracerPhoto` (it never touches your other Python installs), installs PyTorch (the NVIDIA build when it
finds an NVIDIA card, otherwise the processor-only build), installs TripoSR at the version this was tested with, and downloads the
model weights (about 1.7 GB, from Hugging Face) and the background-removal model (U2-Net, Apache-2.0, about 176 MB). Then use the menu as above.

An NVIDIA card with about 6 GB of memory makes a photo take seconds; on the processor it takes several minutes. On a Mac see the next section.

**From the program (Windows).** Open the **Diagnostics** tab, press **Run Diagnostics**, and if its *Photo helper* section
lists something missing, press **Install Photo Helper...**. It says what will be downloaded and from where, shows the exact script it will run, asks you to confirm, runs the same
setup script with its output in a window (Cancel stops it and everything it started), and runs the diagnostics again when it is done, so the
report shows the result. It needs Python 3.10 to 3.12 and git on your PATH; on macOS and Linux the button is disabled and the guide's other steps apply.

**Checking what is installed.** Run the **Diagnostics** tab: its last section, *Photo helper*, lists the helper script, the
Python environment, each package with its version (PyTorch, Transformers, rembg, xatlas, scikit-image, SciPy, trimesh, NumPy,
Pillow, einops, OmegaConf, Hugging Face Hub, ONNX Runtime), the graphics card PyTorch can use, the TripoSR code, and whether the two
model files are already downloaded. Anything missing or unusable is flagged (and the setup script is named), so a broken install
shows up there instead of as a failed import halfway through a photo. The same list is printed by
`python tools\photo_to_mesh\photo_to_mesh.py --check`.

If you keep the environment somewhere else, set `RAY_TRACER_PHOTO3D_PYTHON` to its `python.exe` and pass `-Folder` to the
setup script. Keep the folder name short: PyTorch's folders are deep and Windows stops at 260 characters.

### On a Mac

The same helper works on macOS; the installer is a bash script that ships inside the app (`Contents/MacOS/scripts/setup_photo_to_mesh.sh`). The easiest way is the **Diagnostics** tab: *Run Diagnostics*, then *Install Photo Helper* (it asks first, shows the output while it runs, and Cancel stops it and everything it started). Or run it yourself:

```bash
bash "/Applications/RayTracerGUI.app/Contents/MacOS/scripts/setup_photo_to_mesh.sh"
```

It needs **Python 3.10, 3.11 or 3.12** and **git**, which a Mac does not have by default. The Diagnostics tab's **Install Photo Helper** button (or the script) looks for a suitable Python on the usual paths, including Homebrew's and python.org's, which a program started from Finder does not have on its PATH; if there is none it downloads a private copy of Python 3.12 (about 25 MB, from the python-build-standalone releases on GitHub, checked against their published SHA-256) into the same folder, so nothing needs installing first except git (`xcode-select --install`). What it found or skipped is in the installer's window and in the log file. You can also bring your own: `brew install python@3.12`, or the installer from python.org. The environment goes to `~/Library/Application Support/RayTracerPhoto` (set `RAY_TRACER_PHOTO3D_PYTHON` to its `venv/bin/python` if you keep it elsewhere).

- **Apple silicon** uses the Apple GPU through PyTorch's MPS backend; if MPS cannot run something the helper says so and finishes on the processor (several minutes). `PYTORCH_ENABLE_MPS_FALLBACK=1` is set for it.
- **Intel Macs** get PyTorch 2.2.2, the last version built for them, and always run on the processor (several minutes per photo).
- The results folder is `~/Library/Application Support/Ray Tracer Project/Ray Tracer/photo_meshes`.

### Where things go

Each photo's result is kept in the program's per-user data folder, in `photo_meshes\<photo name>-<date and time>\`
(on Windows `%APPDATA%\Ray Tracer Project\Ray Tracer\photo_meshes`), as `mesh.obj` and `texture.png`. A saved scene points at
those files, so keep that folder (or copy the two files and choose them again) if you move the scene to another computer.

The Diagnostics report's *Saved Photo Objects* line says how many of these folders there are and how much space they take. Nothing deletes them
automatically (a saved scene may point into one), so delete the ones no scene uses when you want the space back.

You can also run the helper by itself:

```powershell
%LOCALAPPDATA%\RayTracerPhoto\venv\Scripts\python.exe tools\photo_to_mesh\photo_to_mesh.py photo.jpg --out out_folder
```

`--resolution 384` gives a finer mesh (slower), `--texture-size 2048` a sharper texture, `--no-remove-bg` skips the
background removal for a photo that already has a plain background.

### Taking a good photo

One object, in the middle, filling about two thirds of the picture, shot from slightly above and from the front (three-quarter
views work well), against a plain background, in even soft light. Avoid glass, mirrors and very shiny things (the model sees
the reflections as part of the object), and objects cut off by the edge of the photo.

### If it does not work

| Message or symptom | What to do |
|---|---|
| "The photo helper has not been set up" | Run the setup script above. The Diagnostics tab shows what is missing. |
| "found no object in the photo" | Use a clearer photo of one object on a plain background. |
| runs out of graphics memory | Close other programs that use the GPU, or run the helper by hand with `--device cpu` (slow). |
| the object comes out lying down or upside down | Use the Rotation fields on the object (the model assumes the photo shows it upright). |
| setup fails with a "path too long" error | Pass a shorter `-Folder`. |

## Room from a photo

**Add > Room from a photo...** turns one photo of a room into a room you can edit: its **floor, ceiling and walls** at about the right size and colour, the **windows and
lamps** as lights, a **camera** that sees what the photo saw (so the first picture you render is the photo's view) and, if you ask for it, the **furniture** it recognises. A small depth model ([Depth Anything
V2](https://github.com/DepthAnything/Depth-Anything-V2), "Metric Indoor Small", 25 million parameters) guesses how far away every pixel is, and a geometry step fits a box-shaped room to that.
It runs on your own computer; **the photo is not uploaded anywhere**. The room **replaces the current scene** (Undo brings it back).

![On the left, a picture of a dining room (the bundled Breakfast Room scene, rendered, used as the test photo); on the right, the room made from it: walls, floor, a lamp, and three chairs and a table in the same places](gallery/room-from-photo.jpg)

*Left: the photo (here a render of a view of the bundled Breakfast Room scene, because its true layout is known). Right: the room the program made from it, rendered from the camera it placed. The floor's tiles and grout, the painting on the wall, the corner and the brightness come from the photo; the table (measured boxes: a top and four legs) and the chairs (3D models of the photo's chairs) stand where the photo has them. The lamps, the window's view and small things do not.*

It is a first draft, not a scan:

- the **furniture is a guess** (the next section): only what the model can name is found, each piece has an invented back, and the room itself is empty of everything else (pictures, rugs, small things: add them with *Object from a photo*, shapes or the model library);
- a **very wide-angle** photo (a small room shot from a doorway) is read as a longer, narrower room than it is, and the furniture in it comes out small; set the field of view yourself (the Camera section, or the field-of-view box when asked) and a piece standing on the floor is always kept to a believable height (a bed at least 0.4 m, a chair at least 0.65 m);
- a wall must be **at least 1.2 m wide** to count as the end of the room: a doorway or a narrow recess that leads further (a hall, a kitchen seen through a door) is not the back wall, so it shows as a picture on the wall, not as a deeper room;
- a wall, floor or ceiling the photo **does not show is assumed** (the program lists those after it finishes), and what it does not show is taken to run square;
- the room is a **box, or an outline of straight walls at right angles** (an L, an alcove, a bay, a chimney breast, a window set in a deep recess): when the photo's walls do not sit on one rectangle, the program reads the outline off the walls it shows and makes one wall per edge (the notes say how many). A sloping ceiling, a wall at another angle, a curved wall and a different ceiling height in each part are not modelled (the ceiling is one flat height, and the outline is made of walls at right angles to each other);
- the **size** is only as good as the depth model's sense of scale: it is usually within a fifth, sometimes further. If you know how high the camera was above the floor, say so in the dialog
  and everything is scaled to match;
- the **surfaces carry the photo's own look**: for every point of the floor, ceiling and each wall the photo shows, the program finds the pixel that sees it and takes its colour, so a wood floor keeps its grain, a tiled one its grout, and wallpaper and the pictures hanging on a wall come with the wall. A point something stands in front of (a chair in front of a wall) is skipped, found by comparing the depth the photo measured with the point's distance, and what the photo does not show or furniture hid is filled in smoothly from what it does, so a hidden patch of floor is a soft blend of its surroundings, not more grain. The colours are brightened like a flat colour would be (a lit photo is not a reflectance, so a dark room's walls are only partly brightened), and what the photo shows only a little of keeps one flat colour. The pictures are in `textures\` beside `room.json`;
- a patch that is clipped to white in the photo (a window, a lamp) becomes an emitting panel on the wall or ceiling, and a window shows what the photo shows there (the panes, the blinds, a view when the exposure kept one: its pixels are cut out as the panel's *Light picture* in the Builder, so it glows in those colours; photo windows are mostly overexposed white, so what you usually gain is the window's structure, not a landscape) it is on, and if the photo has none, one light goes in the middle of the ceiling.
  The **light level** is set by rendering a small draft of the room and turning the lights until its average brightness matches the photo's, so the rendered room is about as bright as the
  photographed one (the first render also shows how far off the colours are: change them in the properties).

### Furniture

The dialog's **Furniture to add** (0 to 8, 4 to start with) is how many pieces to look for. A second small model (DETR, Apache-2.0, about 170 MB, downloaded the first time) outlines and names what is in the photo; the program keeps the chairs, sofas, beds, tables, plants, shelves, cabinets, appliances and a few other things it knows (not people or animals) that cover at least about 1% of the picture, the biggest first, then builds each one. **Tables** are a top the size of the visible top (read from the rays through its pixels, since a top is seen at a grazing angle) on four legs to the floor (the top carries the photo's own picture of it, its wood or its cloth, taken only from the pixels the photo shows as the table, so the floor round a round table does not end up on its top; the top itself stays a rectangle), **stairs** are the Builder's own stairs object, measured: the run is whichever way the points spread further, it climbs the way they rise, the rise is their height with the base on the floor, a step is about 18 cm and a flight is taken to be at least 0.9 m wide (the far side hides behind the steps); and **plain blocks** (a fridge, an oven, a cabinet, a counter, a bed) are one box as wide and tall as the visible side, as deep as the top if it is seen, else a typical depth, and out to the wall if one is just behind: both are measured, with no guessing, and built from Builder boxes (one group, so a table is picked and moved as one). Everything else (chairs, sofas, plants, a TV...) is cut out and the Object-from-a-photo model (TripoSR) makes a 3D mesh of it. Each is stood where the photo shows it: how tall it is and where it stands come from the depth, and it faces the camera (or lines up with the room's walls when it is nearly so, like a sofa against a wall). Its colours are brightened the way the walls are. A piece takes a few seconds on a graphics card and minutes on the processor.

Expect a draft you tidy up: a piece that is **cut off by the picture's edge or half hidden behind another** comes out lumpy or in the wrong place (the model cannot see what is not there); a 3D model's **thin legs and open frames** (a chair's back) come out solid or missing (tables are measured boxes, so theirs do not), and furniture that stands on the floor is put on it whatever its legs showed; and the model only knows common household things (COCO's categories), so a piano or an unusual lamp is left out. Move, resize or delete any piece in the Builder; the notes list what was made. The cut-out each piece was made from is saved next to it as `cutout.png`. Everything the segmentation model names as a thing in the room (furniture, stairs, bags, toys, a plant) is also **left out of the walls' and floor's pictures**, so a staircase or a chair is not painted on the wall behind it as well as being modelled; what it hid is filled in softly.

The dialog also asks for the photo's **field of view** (how wide it sees, left to right). Leave it at 0 and the program reads it from the photo's own data (the 35 mm equivalent focal length
that phones and cameras store), or assumes a phone camera's about 67 degrees; a room that looks stretched or squashed in the first picture has the wrong value, and the camera's field of view
in the Camera section fixes it.

### Setting it up

It uses the **same helper environment as Object from a photo** (the setup above: *Diagnostics > Install Photo Helper*; furniture also needs the small `timm` package, which the setup script now installs: if you set the helper up earlier, run the setup script again or `pip install timm` in its environment). The first run downloads the depth model (about 100 MB, from Hugging Face) and says so; furniture downloads its model (about 170 MB) the first time it is asked for. After that it works offline. Unlike the object helper it needs no graphics card: the model is small, a photo takes a few seconds on the processor and about a second on a graphics card.

The depth model is a fine-tune of Depth Anything V2's "Small" model, which is released under the Apache-2.0 licence; the metric fine-tune's own model card does not name a licence, so look at
the [model page](https://huggingface.co/depth-anything/Depth-Anything-V2-Metric-Indoor-Small-hf) before using results commercially. The model is downloaded on your computer and never shipped with the program.

### Taking a good photo

Stand in the room, hold the camera at eye level and level (not tilted much), and show **the floor, the corner where two walls meet, and some ceiling**. Wide views work best (a phone's main
camera is fine). Avoid mirrors and large windows with a bright view (the model reads them as holes in the wall) and very dark rooms. One room per photo.

### Where things go

Each room's result is in the same per-user folder as the photo objects, in `photo_meshes\room-<photo name>-<date and time>\`: `room.json` (the room: its boxes, lights, camera and what was assumed), `textures\` (the pictures of its surfaces) and
`depth.png` (the depth map as a grey picture, white = near, to see what the model saw). The scene itself is an ordinary Builder scene made of boxes: save it like any other.

You can also run the helper by itself:

```powershell
%LOCALAPPDATA%\RayTracerPhoto\venv\Scripts\python.exe tools\photo_to_room\photo_to_room.py photo.jpg --out out_folder [--fov 65] [--camera-height 1.5]
```

### If it does not work

| Message or symptom | What to do |
|---|---|
| "The photo helper has not been set up" | Install the photo helper (Diagnostics tab), as for Object from a photo. |
| "could not find a room in this photo" | Use a photo taken inside a room that shows a floor and at least one wall. |
| "No furniture was recognised in the photo" | Nothing the model knows covered enough of the picture; move closer, or add pieces yourself. |
| a piece is black, lumpy, floating or in the wrong place | It was cut off by the picture's edge, half hidden or thin: delete it and use *Object from a photo* on a clearer picture of it. |
| the room is the wrong size | Say how high the camera was in the dialog (the size follows it), or scale the room's objects in the Builder. |
| the room looks stretched or squashed in the first picture | The photo's field of view was guessed wrong: give it in the dialog, or set the camera's field of view. |
| the walls are crooked | The photo was tilted or showed mostly one wall; try one that shows a corner and some floor. |
| runs out of graphics memory | Close other programs that use the graphics card, or run the helper by hand with `--device cpu`. |

## A photo scan (many photos)

For an accurate copy of a real object or place, take 30 to 100 overlapping photos while moving around it and let
photogrammetry software reconstruct it. Free choices:

- **[COLMAP](https://colmap.github.io/)** (command line and GUI): finds the camera positions and a dense point cloud, then
  *Poisson meshing* gives a mesh.
- **[Meshroom](https://alicevision.org/#meshroom)**: a friendlier interface over the same kind of pipeline, and it can bake
  a texture for you.
- **Apple's Object Capture** (macOS) and phone apps (Polycam, Scaniverse, RealityScan, Luma) do the whole job and export a mesh.

Export a **mesh with a texture** as `.obj` (or `.ply`), then in the Scene Builder:

1. **Add > Mesh...** and choose the file. Set **Scale** until it is the size you want (scans come out in arbitrary units).
2. Set the material's **Picture** to the texture image the scan came with.
3. Add lights and a sky, and render.

Things that do not carry over yet:

- A scan with **several textures or materials** (a mesh made of separate pieces, each with its own picture) shows one picture
  over the whole mesh; use one texture atlas when you export.
- A mesh that has only **per-vertex colours** (no texture) renders in one flat colour. Bake the colours to a texture in
  MeshLab or Blender, or export with a texture.
- Scans are **not relightable** beyond the colour they carry: shadows and highlights from the day they were photographed are
  in the texture. Photograph in even, overcast light to keep that small.

## Why is the furniture only a guess?

One photo does not hold enough information to rebuild a room's furniture: depth is ambiguous and everything hidden is unknown. A room is different, because it is mostly a box: the floor, the
ceiling and a few walls, which a depth map shows well, so *Room from a photo* builds that reliably. Furniture is the same job as *Object from a photo* (one object, one view, an invented back), done several times, with the extra problem that pieces hide one another, so it is offered as a starting point to edit. Neural scene models (NeRF, Gaussian splatting) give beautiful new views of a scene but are not geometry and materials a path tracer can re-light, so they are not offered here.

## For developers

- *Room from a photo* is two Python files that need no model for their tests: `tools/photo_to_room/room_fit.py` (the geometry: depth map to a box room, colours, lights, camera; numpy only) and
  `room_outline.py` (the outline of a room that is not one rectangle: a boundary sample per image column, runs of walls along x or z, a chain of corners and steps, closed round what the photo does not show) and `room_textures.py` (the pictures of the surfaces, read off the photo with the fitted camera) and `photo_to_room.py` (the model wrapper). `python -m unittest tools/photo_to_room/test_room_fit.py` fits rooms of known size photographed analytically (a turned, tilted and rolled camera, noisy
  depth, walls out of view) `test_room_outline.py` (an L and an alcove photographed with exact ray casting come out as their outlines, a plain box stays a box, every wall faces into the room) and `test_room_textures.py` (a room painted with "where on the surface am I": the pictures show it where they should, upright, with furniture left out and holes filled) and `test_room_objects.py` (furniture of known size and place: where it stands, how tall, which way it faces, what counts as furniture, the texture's exposure). The Builder's side is `src/shared/scene_room.h` (reads `room.json`, builds the objects, and the light-level solver) with `tests/unit/scene_room_tests.cpp`, which also renders
  a room to check that the photo's left stays left. The GUI self-test mode `room` (`RT_GUI_SELFTEST=room RT_GUI_SELFTEST_ROOM_PHOTO=<image>`) runs the whole flow and writes the room as
  `<prefix>_room.pbrt` (`RT_GUI_SELFTEST_ROOM_FURNITURE=<n>` asks for furniture too).
- The helper's pure parts have tests that need no model: `%LOCALAPPDATA%\RayTracerPhoto\venv\Scripts\python.exe -m unittest tools/photo_to_mesh/test_photo_to_mesh.py`
  (mesh orientation, the colour baker, the `--check` report; they skip without numpy and scipy).
- The Qt-free text handling (`src/shared/photo_helper_report.h`: progress lines, the missing-facts parser, the line splitter that copes with
  `\n`, `\r\n`, PowerShell's `\r\r\n` and pip's lone `\r`) is in `tests/unit/photo_helper_report_tests.cpp`.
- The GUI self-test modes `photo` (needs the helper installed) and `installphoto` (stand-in installer scripts, incl. Cancel; `python3 scripts/gui_selftest.py APP --modes installphoto`) drive the whole flow without dialogs; see
  `qt_gui/mainwindow_selftest_photo.cpp`.
