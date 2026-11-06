# The Last Signal — visual fidelity review

Reviewed on `nomai-reference-alignment`, 2026-11-05. Keep this branch unmerged.

## Art direction and verdict

Preserve the film's original story, invented moon, and observatory. Match Outer Wilds' visual design without presenting these locations or the original family as canonical game content. This follows the author's clarification during this review.

**The assets are improved reference-informed reconstructions, not a fully verified reproduction of the game.** The review covers all nine world/craft catalogue models, the shared explorer, five family variants, exports, and the connection to the existing film. It does not certify every unseen surface, room, animation frame, or original game asset. More polygons alone do not establish fidelity.

## Reference evidence

- [Ian Jacobson's Outer Wilds portfolio](https://www.ianjacobson.com/Outer-Wilds): the supplied suit, MaskB, statue, suspension bridge, Escape Pod B, and Tower Twin Exterior sheets. The previously downloaded sheets were inspected directly during this review. These establish the design vocabulary; concept sheets are not automatically identical to shipped models.
- [Lara Colson's environment breakdown](https://laracolson.artstation.com/projects/8l32Pw): separate Brittle Hollow fragments, layered terrain construction, Giant's Deep islands, and Ember Twin cave/rock work. The article was accessible; further image retrieval was denied, so it was not used to claim unobserved geometric matches.
- [Gus Lambrechtsen's Hourglass Twins work](https://ghostygus.artstation.com/projects/5Lq98): retained from the initial reference inventory; precise settlement layout remains unverified.
- Solanum's previously collected in-game turntable still was compared with the adult suit. It does not establish child or elder anatomy.

## Findings and changes

| Asset | Changes or review result | Still not established |
| --- | --- | --- |
| Explorer suit and mask | Retained corrected shorter adult proportions. Added crest constellation inlays and inset lens lattice. Added cheek locks, brows and nostril definition. Corrected the removed mask's scaled grip location to meet the left palm. | Exact suit proportions and weathering; exposed living face; footwear shape; all motion/contact poses. The mask-lifting transition still needs authored hand animation. |
| Family: child, adolescent, two adults, elder | All five profiles retained and rendered; they share the improved mask/face construction. Foot-lock checks cover every age under rotation and translation. | Age ratios, horn development, fur, clothing variation, and relationships are original artistic interpretations. |
| Gravity-cannon shuttle | Reviewed all source components and retained the existing upright reconstruction and gravity-lift ring. No unsupported engine details were added. | Hull silhouette, cabin, underside, control sphere assembly and surface materials need better direct game views. This is the least detailed craft in the catalogue. |
| Escape pod | Replaced separate box segments and radial cage with continuous paired swept protection plates and integrated lower scrolls. Added stepped relief, canopy ribs, keel and framed hatch. Fixed inward-facing sweep geometry. | Cabin proportions, glazing, entry/exit routes, interior computer, deployed escape structures and damage state. Exterior silhouette remains an approximation. |
| Ash Twin solar tower | Five tiers of blue panels, nested copper hexagons, radial lines, rear-face circuitry, fluted spine and spreading root supports. | Exact panel backing, glass response, wear, proportions and base attachment. This solar assembly is not the complete network of Ash Twin warp towers. |
| Giant's Deep | Broadened/tapered cloud ribbons; removed nonperiodic longitude colour sampling. | Volumetric clouds, polar cyclone, ocean, submerged layers, islands, orbital structures and close-up atmosphere. Current model is an exterior globe study. |
| Brittle Hollow | Added inner crust and cut-edge thickness. Removed an invented luminous equatorial ring around the black-hole marker. | True fragment layout, polar ice, settlements, volcanic moon geography, black-hole lensing and destruction animation. |
| Hourglass Twins | Inherits detailed solar assemblies; corrected OBJ precision so small tower details survive serialization. | Sand dynamics, cave network, exact canyon geography, tower types and all major facilities. |
| Trial-tower island | Added coastal rocks, paired ramp studies, three-diamond motif, roof details and properly scaled masonry courses. | Exact tower silhouette, trial rooms, quantum devices, full stair routing and island vegetation. Ramp geometry is an approximation, not a reconstruction of the puzzle route. |
| Hanging City district | Added bridge paving, diamond borders, layered cornices, carved pylons/stays, roof seams, thresholds, benches and oculus rims. Connected each suspension post to its own rock anchor. | Full districts, gravity paths, Black Hole Forge, school, shrine, exact building silhouettes and wear. |
| Sunless City terraces | Inherits dwelling construction detail; added staggered irregular sandstone backdrop. Kept entry paths clear of interior benches. | Actual district layout, cave enclosure, sand levels, plants, lighting controls, murals and props. |
| Original film world/ruins/craft | Source integration reviewed: the movie uses `nomai_world.h` and `nomai_ruins.h`, not the nine catalogue studies. Shared character improvements affect it; its story layout remains intact. | The original horizontal film craft and invented observatory are artistic designs. The catalogue shuttle is a separate model; improving it does not silently replace the film craft. |

## Geometry and export corrections

- OBJ now exports authored normals, transformed by the inverse transpose for nonuniform scale.
- OBJ uses 17 significant digits. The previous default precision collapsed miniature Hourglass Twins details into zero-area triangles; validation reads the actual written file to catch this.
- Continuous escape-pod plates have outward winding and closed end caps, with no internal box end faces along each sweep.
- Overhead crust uses fully three-dimensional fragments. Ground-scatter rocks have deliberately flattened undersides and were unsuitable for this purpose.
- The generators now produce front, side and rear scenes in addition to the original three-quarter scene for every catalogue model.

## Validation

- Windows Release launcher and referenced renderer projects built with MSBuild (`TrackFileAccess=false`).
- `nomai_asset_review`: all nine models have finite vertices, valid indices and finite normals; continuous-plate outward winding and longitude seam checks pass.
- `check_nomai_exports.py`: all nine serialized OBJ/MTL pairs have finite unit normals, valid vertex/normal/material references, and nonzero triangle areas.
- `nomai_review`: mask geometry including new inlays passes finite/index/area/normal checks; removed-mask grip meets the palm under three head rotations.
- Family foot-lock checks pass for five ages and five walking phases under a translated and rotated frame.
- `--film-check` passes for the family film: 8 seconds, 24 fps, 3 shots, 5 characters. A frame was rendered through the actual `.film` path.
- GPU wavefront review renders cover all nine catalogue models, selected side/rear views, family lineup and elder, mask close-up, and mask-lifting/removal, walking and crouching poses.
- Build-list, code-size and whitespace checks pass.

The full renderer test suite, Windows CPU rendering, recursive OptiX, macOS/Metal, every family gesture, and the complete 300-second movie have not been validated in this pass. Renders are inspection evidence, not an assertion that every outstanding art item above is complete.

## Reproduction

Compile `scripts/anim/nomai_asset_review.cpp` and `scripts/anim/nomai_review.cpp` as C++17. Run:

```text
nomai_asset_review SCENE_DIR MODEL_DIR all
nomai_review SCENE_DIR
python scripts/anim/check_nomai_exports.py MODEL_DIR
```

The first generator writes nine OBJ/MTL pairs, 36 world/craft view scenes and six family scenes. The second writes four explorer studio scenes and four pose scenes. Render with the repository launcher using `--gpu --wavefront --denoise`, then inspect side/rear views and close-ups as well as the main gallery.

No original game meshes or textures were extracted or imported.
