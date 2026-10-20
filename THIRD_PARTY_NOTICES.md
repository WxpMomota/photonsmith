# Third-party notices

The original source code in this repository is under the [MIT licence](LICENSE).
Everything below was written by someone else, or is derived from their work, and
keeps its own licence. If you redistribute this project (or part of it), these
are the terms you inherit.

## Code that is in the repository

| Component | Where | Licence |
|---|---|---|
| **pbrt-v4** (Matt Pharr, Wenzel Jakob, Greg Humphreys) | Ported or adapted code in `src/shared/` (BDPT, MLT, SPPM and the other integrators, light samplers and light BVH, BxDFs, shapes, cameras, spectral code, sampling and hashing helpers, ReSTIR, BSSRDF, ...) and in the GPU kernels that mirror it. Roughly 45 files carry the pbrt-v4 attribution header, and each says what it was ported from. | Apache-2.0, full text in [`licenses/Apache-2.0-pbrt-v4.txt`](licenses/Apache-2.0-pbrt-v4.txt). Copyright (c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys. The ports are modified: they are rewritten as CPU/GPU templates against this project's scene interfaces, and several are adjusted where the original behaviour differs (each file's header and comments say so). Those files stay under Apache-2.0. |
| **Ray Tracing in One Weekend** series (Peter Shirley and others) | The first scenes, camera, vector and material code in `src/TheRestOfYourLife/` (each file's header says so) | CC0 1.0 Universal (public domain) |
| **NanoVDB** (Contributors to the OpenVDB Project) | `src/external/nanovdb/` | Apache-2.0 (SPDX header in the sources) |
| **tinyexr** (Syoyo Fujita and contributors; contains code derived from OpenEXR, Industrial Light & Magic) | `src/external/tinyexr.h` and `exr_reader.hh`, `streamreader.hh` | BSD-3-Clause; the licence text is at the top of the header |
| **Hosek-Wilkie sky model** (Lukas Hosek and Alexander Wilkie; coefficient tables as shipped in Blender's Cycles) | `src/shared/hosek_sky.h` (a re-write of the evaluation code) and `src/external/hosek_sky_data.h` (the tables, copied unchanged) | BSD-3-Clause, full text in [`licenses/BSD-3-Clause-Hosek-Wilkie.txt`](licenses/BSD-3-Clause-Hosek-Wilkie.txt) |
| **stb_image / stb_image_write** (Sean Barrett and contributors) | `src/external/stb_image.h`, `stb_image_write.h` | Public domain (or MIT, at your choice); see the end of each header |
| **miniz** (Rich Geldreich and contributors) | `src/external/miniz.c`, `miniz.h` | Public domain ("unlicense" statement at the end of the file) |
| **Noto Sans SC** (the Source Han Sans design: Copyright 2014-2021 Adobe, Reserved Font Name 'Source'; distributed by Google as Noto Sans CJK) | `qt_gui/fonts/NotoSansSC-Regular.ttf`, compiled into the GUI so Simplified Chinese text renders on a machine without a CJK font | SIL Open Font License 1.1 (`qt_gui/fonts/OFL.txt`; the release packages carry it as `licenses/NotoSansSC-OFL.txt`) |

## Tools and libraries used but not stored here

| Component | How it is used | Licence |
|---|---|---|
| **Qt** | The desktop GUI (`qt_gui/`) is built against your own Qt install and linked dynamically in the release packages | LGPL-3.0 (and commercial). Qt's source and licence: https://www.qt.io/ |
| **NVIDIA CUDA Toolkit** | Needed to build the OptiX backend. The runtime DLLs that the Windows release packages include are NVIDIA's redistributable files | NVIDIA CUDA Toolkit EULA |
| **NVIDIA OptiX SDK** | Needed to build the OptiX backend. **Not included in this repository or in the releases**: install it yourself and accept NVIDIA's licence | NVIDIA OptiX SDK licence |
| **Apple Metal / Foundation frameworks** | The macOS GPU backend uses the system frameworks | Apple's system licence |
| **GoogleTest** | The unit tests (fetched by CMake / vcpkg at configure time, not stored here) | BSD-3-Clause |
| **FFmpeg** (the `avcodec`, `avformat`, `avutil`, `swresample` and `swscale` DLLs) | Shipped in the Medium and Full packages because Qt's multimedia backend (the Live Preview video and the in-app video preview) needs them; we do not modify them | LGPL-2.1-or-later. Source and licence: https://ffmpeg.org/ |
| **Intel Open Image Denoise** | The AI denoiser for the CPU and Metal renderers. **Not in the repository or the packages**: the app offers to download Intel's own release once (about 57 MB on Windows), checked against a pinned SHA-256, and loads it at run time | Apache-2.0. https://www.openimagedenoise.org/ |
| **TripoSR** (Stability AI and Tripo) | The optional "Object from a photo" helper. **Not in the repository or the packages**: `scripts/setup_photo_to_mesh.ps1` downloads the model on request (about 5 GB) | MIT (its repository and model card; check them before you redistribute the weights). https://github.com/VAST-AI-Research/TripoSR |
| **Microsoft Visual C++ runtime** (`vcruntime140*.dll`, `msvcp140.dll`) | Redistributed in the Windows packages | Microsoft's redistributable terms |

## Meshes, textures and scenes

These are data, not code, and the MIT licence **does not apply to them**.

* **`models/`** holds test meshes from public 3D test collections: the Stanford 3D
  Scanning Repository, the McGuire Computer Graphics Archive (Crytek Sponza, Amazon
  Lumberyard Bistro, Rungholt) and the common-3d-test-models collection. Each model
  keeps the licence and attribution terms of its source. Some require attribution,
  some allow research use only, and some are non-commercial, so check the source
  before you redistribute a model or ship it in a product. `docs/MODELS.md` has a per-model
  table of sources (it was not checked file by file; the sources stay authoritative). The
  installers carry only Spot, Suzanne and the Utah teapot.
  The larger scenes (Power Plant and others) are deliberately not bundled in the
  installers for this reason.
* **`models/gltf/`** holds 56 free glTF models: from the Khronos glTF Sample Assets (CC0 or CC BY 4.0, each model its own), Poly Haven (CC0) and Kenney's Furniture Kit (CC0). All were changed to keep the
  repository small (pictures made smaller, pictures this program does not read left out); `models/gltf/CREDITS.md` names the source, licence and credit of each one. The CC BY 4.0 models need the credit:
  Velvet sofa: Eric Chadwick, Wayfair, LLC (2021), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/GlamVelvetSofa;
  Sunglasses: Eric Chadwick, Darmstadt Graphics Group GmbH (2024), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SunglassesKhronos;
  Glass candle holder: Eric Chadwick, Wayfair, LLC (2021), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/GlassHurricaneCandleHolder;
  Shoe: Shopify, Shopify (2021), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/MaterialsVariantsShoe;
  Silk pouf: Eric Chadwick, Copyright 2023 Wayfair, LLC (2023), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SpecularSilkPouf;
  Traffic cone: hinndia, hinndia (2024), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/TrafficCone;
  Car concept: Eric Chadwick, Darmstadt Graphics Group GmbH (2024), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CarConcept;
  Broken window: Eric Chadwick, Wayfair (2023), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/GlassBrokenWindow;
  Iridescent dish with olives: Eric Chadwick, Wayfair, LLC (2020), https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IridescentDishWithOlives. Licence: https://creativecommons.org/licenses/by/4.0/ . The installers do not carry them.
* **`environments/`** holds 30 high-dynamic-range sky and room panoramas (1k) from Poly Haven (polyhaven.com), published under CC0 1.0 (public domain dedication): unchanged copies of Poly Haven's `.hdr` files and its preview
  pictures. No credit is required; `environments/CREDITS.md` names each one's author and source page. The installers do not carry them.
* **Benedikt Bitterli's rendering-resources scenes** (19 scenes, H22 to H38, H41 and H42): **not in the repository or the installers**. The Scene Builder's "Download missing files" fetches them from https://benedikt-bitterli.me/resources/ on request (their pbrt-v4 versions), and they are kept out of git. Each scene keeps its own licence, CC0 or CC BY 3.0, and its `LICENSE.txt` is downloaded with it; the CC BY 3.0 ones need the credit: The Grey & White Room, The Wooden Staircase and The Modern Living Room by Wig42; Country Kitchen and The White Room by Jay-Artist; Modern Hall by NewSee2l035; Coffee Maker by cekuhnen; Japanese Classroom by NovaZeeke; Dragon by Delatronic. Licence: https://creativecommons.org/licenses/by/3.0/ . The CC0 ones are Bedroom (SlykDrako), Glass of Water (aXel), Pontiac GTO 67 and 4060.b Spaceship (thecali), Old vintage car (piopis), Little Lamp (UP3D), Victorian Style House (MrChimp2313) the Material Test Ball, Veach, Ajar and the Water Caustic (Benedikt Bitterli).
* **Scenes from the pbrt-v4-scenes collection** (https://github.com/mmp/pbrt-v4-scenes: Contemporary Bathroom, Barcelona Pavilion, Subsurface Dragon, Ganesha, Sports Car, Zero Day, Crown, Villa, Transparent Machines, Landscape, BMW M6, Kroken and Watercolor): **not in the repository or the installers**; "Download missing files" fetches each from that repository, at a fixed commit, on request. Each keeps its own licence (`scripts/gen_scene_packs.py` and the download dialog carry the credit): **Landscape** is by Jan-Walter Schliep, Burak Kahraman and Timm Dapper of Laubwerk (https://www.laubwerk.com) under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/); **BMW M6** is by tyrant monkey on Blend Swap, CC0; **Kroken** and **Watercolor** are interiors licensed from Angelo Ferretti (https://www.lucydreams.it), available in pbrt's scene format under CC BY-ND 2.0 (https://creativecommons.org/licenses/by-nd/2.0/): they are fetched unchanged.
* **`images/`**: `earthmap.jpg` is NASA's Blue Marble in equirectangular projection
  (from Wikimedia Commons, public domain: created solely by NASA). `bump-marble.png`
  and `normal-checker.png` are small textures generated for this project's tests.
* **`qt_gui/sample_photos/`** (built into the GUI, for trying the photo features): five free-licence
  photos from Wikimedia Commons, each scaled to 1280 pixels across (a change; nothing else was
  done to them). `beach.jpg`: Dietmar Rabich, "Juist, Strand -- 2014 -- 3658", CC BY-SA 4.0.
  `north-sea.jpg`: Dietmar Rabich, "Norderney, Nordstrand -- 2016 -- 5172", CC BY-SA 4.0.
  `heath.jpg`: Dietmar Rabich, "Vreden, Zwillbrocker Venn -- 2016 -- 4122", CC BY-SA 4.0.
  `meadow.jpg`: vastateparksstaff, "Touch the Sky at Sky Meadows", CC BY 2.0.
  `overcast.jpg`: paul morris oldskool2016, "Overcast sky over Snowdonia", CC0 (public domain).
  The photos keep their own licences (the three CC BY-SA photos stay under CC BY-SA 4.0, which
  applies to the photos and to nothing else in this repository); the source page of each is
  `https://commons.wikimedia.org/wiki/File:` followed by the title, and the program's photo picker
  shows each credit.
* **`pbrt_scenes/`**: a scene whose header says "Original, not derived from any
  distributed scene" is original to this project and under its MIT licence. Scenes
  taken or derived from the pbrt-v4 scene collection (https://github.com/mmp/pbrt-v4-scenes)
  keep the licence of that collection and of each scene's author. Several of those
  are non-commercial or no-derivatives, so check before redistributing anything you
  download into that folder.

If you find an attribution that is missing or wrong, please open an issue.
