# Fill and distance Simulator regression

Configure [development paths](../../docs/development.md), build the plugin, then
run from the repository root:

```sh
python3 tests/run_simulator.py tests/sdf_simulator --plugin src/mac/build/Release/plugin_geometry2d.dylib
bash tests/sdf_native/run.sh
bash tests/earcut_backend_native/run.sh
python3 tests/run_simulator.py tests/api_simulator
```

This project loads the example shader through repository-relative paths; run it
in place, not with the self-contained-project `--local-plugin` staging option.
No private project or screenshots are required. The runner requires the explicit
`SIMULATOR_TEST_EXIT: 0` marker. Run benchmarks without competing Simulator loads.

## Coverage

- `contracts.lua`: convex/concave/hole fixtures, coverage and triangle-centroid
  probes against independent segment distances, indexed/triangles equivalence,
  original-bounds UVs, input invariance, invalid options and bounded failures.
- `main.lua`: packed/direct mesh initialization, transparent pending state before
  bulk attributes are ready, owning-buffer survival across calls/GC and replacement.
- `lightweight.lua`: fill with/without vertex AA, exterior-only distance,
  concave/hole opacity, adjacent AA, transformed cached meshes and rebuild timings.
- `fill_outputs.lua`: table/buffers/direct mesh first-frame alpha, tint, holes
  and material UV invariance across AA widths.
- `local_stroke.lua`: approximate `method="local"` distance, readiness, translucent
  corners, holes, cached paint/width changes, transforms and material UVs.
- `pixels.lua`: scales .2/.5/1/2/5, rotation, nonuniform scale, reflection and
  screen-distance metrics; numeric color, opacity, seams, transitions and texture UVs.
- `benchmark.lua`: first/repeated API calls and 32/128/512 cached meshes;
  frame intervals, draw calls and submitted triangles, not GPU execution time.

The native suites additionally exercise dense contours, multiple holes, inverse
mapping, coverage/capacity and sanitizers. Native allocation accounting measures
C++ allocation payload during a call (output plus temporary storage), not RSS,
GPU memory or pure temporary-storage peak.

## Interpretation

The test-only earcut/fringe baseline lacks an internal stroke distance field;
cost comparisons do not establish equivalent visual capabilities. Cached render
benchmarks use scalar UVs and a simple coverage shader to isolate geometry cost;
production examples use a separate distance extension and retain material UVs.
The Simulator alpha-fill baseline also generates vertex colors, so its timing
is not identical to the native baseline's output assembly.

See [API limits](../../docs/api.md) for local approximation and partition
precision failures. Test completion does not imply arbitrary inputs are supported,
nor does macOS coverage establish other-platform runtime behavior.
