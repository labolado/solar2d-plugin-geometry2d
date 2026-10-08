# Fill fringe corner regression

`native.cpp` checks reflex corners, outer/hole roles, both hole windings,
bevel/round/miter joins, indexed/triangles output, short edges and sharp corners.
Dense local probes check coverage and interpolated distance; finite sharp-corner
output does not establish an exact Euclidean distance field.

Run from the repository root:

```sh
case_build_dir=$(mktemp -d)
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -I src/shared \
  tests/cutout_corner/native.cpp src/shared/fringe.cpp -o "$case_build_dir/cutout-native"
"$case_build_dir/cutout-native"
python3 tests/run_simulator.py tests/sdf_simulator --plugin src/mac/build/Release/plugin_geometry2d.dylib
```

The Simulator coverage is in [sdf_simulator](../sdf_simulator/README.md), not a
separate forwarding project. Configure paths as described in
[development](../../docs/development.md).

## Algorithm boundary

Reflex turns clip incident edge rectangles to complementary bisector half-planes,
interpolating their alpha/distance; no extra corner wedge is emitted there.
This does not globally union bands: nonadjacent edges, rings and groups can overlap.
The narrow-notch cases deliberately print `LIMIT` for overlapping wide bands;
they must not be reported as fixed or as globally nonoverlapping SDF geometry.
