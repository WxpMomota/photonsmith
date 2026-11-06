# The Last Signal: Nomai asset kit

Prepared on `nomai-reference-alignment` for review before merging.

See [the detailed fidelity review](NOMAI_FIDELITY_REVIEW.md) for the subsequent construction/detail pass, validation, and outstanding differences. The author chose to retain the movie's invented setting while matching the game's visual design. These assets are not certified game replicas.

## Character proportions and family

The adult's pelvis and chest were shortened and the shoulders widened. The mask is 15% larger relative to the head. This corrects the previous elongated silhouette without flattening the face or helmet geometry. The standing character's crest is approximately 1.8 metres high; this is a production scale, not a verified canonical measurement.

Five reusable `.film` kinds are registered:

| Kind | Design |
| --- | --- |
| `nomai-child` | 67% adult stature, wider build, larger relative head, small growing horns, no beard |
| `nomai-adolescent` | 84% stature, narrower build, intermediate head and horn proportions |
| `nomai-adult-a` | Corrected adult proportions and sea-green suit |
| `nomai-adult-b` | Slightly shorter, broader adult with a warmer cloth tint |
| `nomai-elder` | Slightly shorter, stooped posture, pale fur and longer beard |

These are original family characters. Nomai developmental anatomy and age-specific clothing are inferred from the statue and suit references; they are not canonical age charts. Existing walking poses, gestures, hand targeting and foot locking remain available. The age-aware foot locks were checked through five walking phases under a translated and rotated frame.

`pbrt_scenes/my-nomai-family.film` is an eight-second staging example. It passes `--film-check`. `nomai_family.h` provides the same people directly to C++ film scenes.

## World and craft models

The model archive contains nine OBJ/MTL pairs and corresponding PBRT review scenes:

- Nomai gravity-cannon shuttle reconstruction
- Older Nomai escape pod with swept protective vanes and lower scrolls
- Giant's Deep cloud globe with geometric cyclonic cloud ribbons
- Brittle Hollow cutaway, inner crystal formations, black-hole marker and Hollow's Lantern
- Hourglass Twins with a sand-transfer column and exposed Ash Twin structures
- Giant's Deep trial-tower island study
- Hanging City suspended district and bridge study
- Sunless City terrace settlement study
- Ash Twin solar-panel tower study

Craft and architecture use approximate metre-scale dimensions. Planet models use normalized radii for easy composition; scale their root transforms for a film shot. PBRT scenes include cameras, lights and a studio floor where appropriate. OBJ models contain only the modeled asset, with diffuse material groups and emission values where used.

These are reference-informed production studies, not replicas of every room, traversal route or original game mesh. They are usable modeling foundations. The shuttle is less directly documented than the escape pod and remains a reconstruction. Brittle Hollow's black hole is represented visually; these models do not add gravitational ray bending or a planetary physics simulation. The film's existing fictional moon and observatory are not automatically replaced by these studies.

## Files and reproduction

- `src/shared/cartoon/nomai_game_assets.h`: craft, planet and architecture generators and OBJ export.
- `src/shared/cartoon/nomai_family.h`: age and appearance profiles.
- `src/shared/cartoon/nomai_explorer.h`: corrected body, mask and material system.
- `src/shared/cartoon/film_kinds.h`: `.film` registration and scaled actor metadata.
- `scripts/anim/nomai_asset_review.cpp`: generate PBRT scenes and OBJ/MTL models.
- `scripts/anim/nomai_family_checks.cpp`: check scaled foot-lock behavior.
- `scripts/anim/nomai_review.cpp`: inspect the main explorer from four angles.

Compile the standalone generators as C++17 in an x64 Visual Studio developer shell. Run `nomai_asset_review SCENE_DIR MODEL_DIR all`. The `family` and `objects` modes generate only the requested group. Render generated scenes from the renderer package directory using `ray_tracer --gpu --wavefront --denoise --height 800 --output OUTPUT.png 1000 96 8 SCENE.pbrt`. The family lineup uses 1920 × 900.

The family scene archive copies its generated PLY meshes and HDR image maps into relative asset paths, so it does not depend on the author's local cache directory.

## Reference sources

- [Ian Jacobson — Outer Wilds](https://www.ianjacobson.com/Outer-Wilds): Nomai suit, MaskB, statue, escape-pod, suspension-bridge and Ash Twin tower concept sheets.
- [Lara Colson — Environments of Outer Wilds](https://laracolson.artstation.com/projects/8l32Pw): planet terrain, island and fragment construction references.
- [Gus Lambrechtsen — Hourglass Twins](https://ghostygus.artstation.com/projects/5Lq98): Ember Twin caves, Sunless City and Ash Twin rock placement.
- [Mobius Digital and Unity — The Art of Outer Wilds](https://www.youtube.com/watch?v=Ww12q6HsmJA): Giant's Deep ocean/clouds, Brittle Hollow destruction and the Twins' sand systems.

All new geometry and textures are authored here; no extracted game models or textures are included.
