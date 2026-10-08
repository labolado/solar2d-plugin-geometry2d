# Inner-stroke prototypes — experimental, not stable API

These standalone tests explore two alternatives to `meshDistance`. Neither is
a production replacement. Translation units and the temporary macOS buffer loader
are not integrated into platform plugin builds or installed globally.

## Reproduce

Configure [development paths](../../docs/development.md), then run from the root:

```sh
python3 tests/inner_stroke_prototypes/run_native.py
python3 tests/inner_stroke_prototypes/run_native.py --sanitize
python3 tests/inner_stroke_prototypes/run_native.py --export
GEOMETRY2D_PROTOTYPE_DATA=/printed/export/directory \
  python3 tests/run_simulator.py tests/inner_stroke_prototypes/simulator --timeout 180
```

Use the exact `EXPORT_DIRECTORY` printed by the exporter. Export requires a macOS
compiler/SDK and configured Corona source root. Simulator baselines also load
`src/mac/build/Release/plugin_geometry2d.dylib`; build it first. Run the Simulator
project in place because it uses repository-relative resources.

Default fixtures include the approved public geometry cases **46, 47 and 48** in
`fixtures/backup_geometry.json`, plus deterministic synthetic convex, hole,
disappearing/splitting core and island cases; native tests also cover degeneracy.
The public file contains only case names and original geometry coordinates, with
an adjacent SHA-256 checksum. Keep the numbers unchanged: rounding or transforming
these precision regressions can hide failures. Native runs verify the checksum;
Simulator runs load the same repository fixture by default. Optional private input
requires `--private-fixtures /path/to/geometry.json` and adjacent `geometry.sha256`.
For matching Simulator exports set `GEOMETRY2D_PRIVATE_FIXTURES` to the same absolute
JSON path. This explicit override replaces the three public cases, not the
synthetic controls. Missing or invalid requested input fails the run.
`extract_fixtures.py /path/to/data.json` extracts geometry only and refuses to
replace a differing fixture. Do not publish additional source data without review;
keep any further private fixtures outside the public tree (for example in `.local/`).

## A: offset/difference regions

`native/region.cpp` normalizes outer-minus-holes regions, unions them, performs a
round negative offset (width 4, arc tolerance .05), clips the core to the source,
and triangulates complementary core/stroke regions. Splitting/disappearance is
normal. Attributes label regions; they are not distances.

Checks compare original/float32 triangle direction and quantized triangle
union/XOR/overlap against the source. The 1e-6 coordinate grid and area threshold
are numerical audits, not exact topology or complete adjacency proofs.
Complex inputs can collapse core triangles after float32 conversion. Failed
partial geometry is diagnostic only, never exported; no triangles are discarded.
Shared core/stroke boundary coordination remains unimplemented. This failure gate
blocks the next AA/material stage, which is also not implemented.

## B: coarse tiles with analytic distance

`native/tiles.cpp` recursively prunes edge candidates using
`distance(center, edge) <= nearest(center) + 2 * boxRadius`. Distance and winding
edges are separate; artificial clipping edges affect sign only. Provably constant
sign cells avoid unnecessary clipping; deep cells have a saturation marker.
Candidate vectors are scanned directly; there is no spatial index yet.

Fixed 4+8 and 6+6 layouts are diagnostic alternatives. Shared slots use 11 edge
attributes plus one count/classification attribute. Overflow subdivides or fails,
never truncates. Limits are 65,536 nodes, depth 18 and 20 million generation work
units; reference auditing is separately counted. These are not Clipper wall-time
or peak-memory bounds. Native checks sample nine interior points per accepted box
against all edges; sign checks exclude the .001 boundary neighborhood. Sampling
is not exhaustive coverage proof.

`simulator/tiled.lua` uses indexed meshes with four distinct vertices per tile
to keep candidate attributes constant. The shader combines fill/stroke opacity
and uses nearest-segment normals with coordinate derivatives for AA, not
derivatives of clipped winding. Interpolated integer counts are rounded.
Packed data is copied into owning userdata; this is not zero-copy. Attributes
are uploaded after two frames while the mesh is hidden. More than 65,535 vertices
fails explicitly.

Cache support reserves width 0–4 plus four local units of AA, pads the root by
four and saturates beyond eight. Tested transform minimum singular value is .2;
smaller scales, wider strokes and arbitrary parent transforms are outside this
contract. Sign clipping uses a .0625 local minimum guard plus float-coordinate
guard. Its macOS sampling evidence is not a portable raster-precision guarantee.
The shader requires 12 extra attributes; smaller device limits are unsupported.
UVs encode the padded test domain, not the final original-bounds material mapping.

## Rendering checks and remaining gates

`simulator/coverage.lua` uses an independent all-boundary CPU color oracle.
Deterministic samples cover edges, offsets, seams/corners, widths 0/1/4, scales
.2/.5/1/2/5, rotation, nonuniform scaling, reflection and subpixel translation.
Ambiguous nearest normals retain opacity/classification checks but are excluded
from exact AA-color comparisons. `display.colorSample()` captures one content
unit on the target engine, not the physical framebuffer or GPU execution time.
Optional `GEOMETRY2D_PROTOTYPE_CASE` selects one exported case; omit it for all
available cases. Capability/baseline probes still run.

`DIAGNOSTIC_COMPLETE` or exit 0 means the diagnostic completed, not that both
algorithms succeeded for every input. Failed-call timing is not a speedup.

Still required before production integration:

- A: precision-aware shared boundaries/retriangulation, then inner/outer AA.
- B: spatial indexing, portable precision and textured original-bounds UVs.
- Both: indexed/triangles and three-output contracts, storage reuse, complete
  same-load FPS/P95/draw-call/memory measurements and target-device validation.
- Stable API/platform integration only after acceptance. Data textures or engine
  changes require a separate scope decision.
