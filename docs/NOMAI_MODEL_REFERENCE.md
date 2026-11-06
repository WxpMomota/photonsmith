# Nomai reference alignment

The procedural explorer was revised against Ian Jacobson's published **Nomai suit**, **MaskB**, and **Nomai statue** sheets on his [Outer Wilds portfolio](https://www.ianjacobson.com/Outer-Wilds). All replacement geometry and textures were authored in this repository.

The [subsequent fidelity review](NOMAI_FIDELITY_REVIEW.md) adds mask inlays, lens lattice, facial sculpt detail and a corrected mask grip, and records remaining limitations across the full asset catalogue.

## Changes

| Feature | Previous model | Revised model |
| --- | --- | --- |
| Mask | Rounded shield, round ports, respirator, blue halo | Three amber diamond windows, stepped ivory/gold frames, three interrupted crest arcs, angular chin housing |
| Cloth | Scattered stars, red patterned borders | Blue-green fabric, broad connected navigation arcs, gold bindings and pale piping |
| Neck and shoulders | Beaded collar, horizontal pad grooves | Square collar, stepped neckline, crossed shoulder quilting |
| Backpack | Long narrow tank | Shorter, thicker transverse ivory tank with gold fittings |
| Head and feet | Exposed antler, tucked beard protruding through mask, small toes | Functional hood/liner, concealed horns with hood on, tucked beard omitted, broader suit feet |
| Staff | Floating crystal in gimbal rings | Circular writing slate in an angular cradle, with luminous spiral on both sides |

The existing walking skeleton, gestures, staff targeting, and glow controls are retained. The face liner reveals the fur continuously as the mask lifts. New mask components have separate cache names; the robe textures are versioned to avoid reusing the old artwork.

## Validation

- Compiled the revised model with MSVC C++17.
- Checked mask indices, finite vertices, nonzero triangle areas, and face-normal orientation.
- Checked finite foot positions at five points in the walking cycle.
- Rendered front, face, rear, and side studio views using the project's OptiX wavefront renderer at 800 × 800, 96 samples, with denoising.
- Rebuilt the cartoon implementation and linked a review launcher against the existing CPU/GPU renderer objects. MSBuild's dependency tracker itself failed with a Windows access error, so the build used the compiler, librarian, and linker directly.

## Fidelity limits

This is a procedural reconstruction of the supplied concept art, not the game's original mesh or a verified vertex-for-vertex replica. Hidden construction and exact proportions are inferred from perspective drawings. The biological face remains an interpretation of the statue sheet; the writing staff is reconstructed in the Nomai device language because the supplied suit sheet does not detail it. Full-film animation and every mask-removal pose have not been rendered in this review.

The source changes belong on `nomai-reference-alignment`; they are not to be merged yet. Existing unrelated files and edits are excluded.

## Reproducing the studio scenes

`scripts/anim/nomai_review.cpp` is a standalone C++17 generator. Compile it in a Visual Studio x64 developer shell, then run the executable with an output directory. It validates the revised mask and writes four PBRT studio scenes. Render each using `ray_tracer --gpu --wavefront --denoise --height 800 --output OUTPUT.png 800 96 8 SCENE.pbrt` from the renderer package directory.
