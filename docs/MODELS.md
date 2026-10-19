# The model library

The Scene Builder's **Add > Model library...** lists the meshes in the `models/` folder next to the app, with a picture of each. Adding one puts it on the floor under
the middle of the view, about 1.6 units across, as one undo step; **Scale** in its properties resizes it. Any other `.obj` or `.ply` file can be added with **Choose another
file...** (or Add > Mesh), and then keeps its own size, so a big scan may need a smaller Scale.

What is in the folder depends on where the app comes from:

* **The installers** (macOS dmg, Windows package) carry three small models whose terms are clear: Spot, Suzanne and the Utah teapot.
* **A source checkout** has all 22 meshes in `models/` (about 66 MB) and the 52 glTF models in `models/gltf/` (about 27 MB), and the library shows every one it finds.

To add your own to the library's folder, put the `.obj` there; only the models named in `src/shared/model_library.h` appear in the library, so add a line to its catalog (and a
picture `models/thumbnails/<name>.png`, made by `scripts/make_model_thumbnails.sh`) to list a new one. Anything else is still available through Choose another file.

## Where the models come from

Each model keeps the licence and attribution terms of its source. **This table was not checked file by file**: it says what is known about each source, and the right
thing to do before publishing pictures or scenes made with a model, or shipping it in a product, is to read the source's own terms.

| Model | Source | What the source asks | In the installers |
|---|---|---|---|
| Spot (cow) | Keenan Crane's 3D model repository | CC0 (public domain dedication) | yes |
| Suzanne | Blender (the Blender Foundation distributes it freely with Blender) | none that we know of | yes |
| Utah teapot | Martin Newell, University of Utah, 1975 | widely treated as public domain | yes |
| Stanford bunny, Armadillo, Happy Buddha, Lucy, Dragon (XYZ RGB) | The Stanford 3D Scanning Repository (graphics.stanford.edu/data/3Dscanrep) | acknowledge the source; the page says some models need permission for commercial use | no |
| Cow, Horse, Ogre, Beast, Homer, Cheburashka, Beetle (two), Nefertiti, Max Planck, Bimba, Igea, Fandisk, Rocker arm | The common-3d-test-models collection (github.com/alecjacobson/common-3d-test-models), which gathers meshes from several places (Stanford, AIM@SHAPE, Keenan Crane, others) | its README names each model's origin; some allow research use only or non-commercial use | no |
| Antique camera | Khronos glTF Sample Assets | CC0 (public domain dedication) | no |
| Avocado, Barramundi fish, Boom box, Water bottle | Khronos glTF Sample Assets | CC0 (public domain dedication) | no |
| Chair (sheen) | Khronos glTF Sample Assets | CC0 (public domain dedication) | no |
| Corset | Khronos glTF Sample Assets | CC0 (public domain dedication) | no |
| Lantern (old) | Khronos glTF Sample Assets | CC0 (public domain dedication) | no |
| Toy car | Khronos glTF Sample Assets | CC0 (public domain dedication) | no |
| Glass candle holder, Velvet sofa | Khronos glTF Sample Assets | CC BY 4.0: credit **Eric Chadwick (Wayfair, LLC)**, link the licence, say it was changed | no |
| Shoe | Khronos glTF Sample Assets | CC BY 4.0: credit **Shopify**, link the licence, say it was changed | no |
| Silk pouf | Khronos glTF Sample Assets | CC BY 4.0: credit **Eric Chadwick (Copyright 2023 Wayfair, LLC)**, link the licence, say it was changed | no |
| Sunglasses | Khronos glTF Sample Assets | CC BY 4.0: credit **Eric Chadwick (Darmstadt Graphics Group GmbH)**, link the licence, say it was changed | no |
| Traffic cone | Khronos glTF Sample Assets | CC BY 4.0: credit **hinndia**, link the licence, say it was changed | no |
| Alarm clock | Poly Haven | CC0 (public domain dedication) | no |
| Apple | Poly Haven | CC0 (public domain dedication) | no |
| Barrel | Poly Haven | CC0 (public domain dedication) | no |
| Camera 01, Lantern 01 | Poly Haven | CC0 (public domain dedication) | no |
| Chess set | Poly Haven | CC0 (public domain dedication) | no |
| Concrete cat statue | Poly Haven | CC0 (public domain dedication) | no |
| Horse statue, Marble bust, Potted plant | Poly Haven | CC0 (public domain dedication) | no |
| Modern arm chair | Poly Haven | CC0 (public domain dedication) | no |
| Rubber duck | Poly Haven | CC0 (public domain dedication) | no |
| Sofa 02 | Poly Haven | CC0 (public domain dedication) | no |
| Tea set | Poly Haven | CC0 (public domain dedication) | no |
| Television 01 | Poly Haven | CC0 (public domain dedication) | no |
| Wine bottles | Poly Haven | CC0 (public domain dedication) | no |
| Wooden table | Poly Haven | CC0 (public domain dedication) | no |
| Bathtub, Bookcase, Coffee table, Design chair, Desk, Double bed, Floor lamp, Fridge, Kitchen stove, Laptop, Lounge sofa (low poly), Modern chair (low poly), Potted plant (low poly), Radio, Round rug, Speaker, Teddy bear, Toaster, Trash can, Vintage television | Kenney Furniture Kit 2.0 | CC0 (public domain dedication) | no |

## The glTF models

`models/gltf/` holds 52 everyday objects and pieces of furniture as `.glb` files, from three free sources: **the Khronos glTF Sample Assets** (github.com/KhronosGroup/glTF-Sample-Assets, which gives each model
its own licence: the ones here are CC0 or CC BY 4.0), **Poly Haven** (polyhaven.com: all CC0) and **Kenney's Furniture Kit** (kenney.nl: CC0). 46 are CC0 and need nothing; 6 are **CC BY 4.0**, which needs a
credit, a link to the licence and a note that the model was changed: `models/gltf/CREDITS.md` has the credit and the source page of every model, and travels with the files. They are added with the glTF importer (see
SCENE_BUILDER.md), at their real size (an avocado is 10 cm, a sofa 2 m), with their colours and materials; a model in the library is a starting point for a scene, so it is worth setting its real height when it is placed in a photo.

To save space every file was changed the same way (the credit file says so): its pictures are at most 1024 pixels across, the pictures this program does not read are left out (the average metal and roughness are kept in
the material's numbers), and everything is in one file. The thumbnails are made by `RT_GUI_SELFTEST=modelthumbs RT_GUI_SELFTEST_MODELS=<models folder> RayTracerGUI` (the real importer and renderer). Models that are
not in the installers: these are all `no` below.

The thumbnails in `models/thumbnails/` are renders made for this project (`scripts/make_model_thumbnails.sh`); they carry no terms of their own beyond the model's.
`THIRD_PARTY_NOTICES.md` says the same in short.
