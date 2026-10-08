# Ribbon Simulator tests

Configure [development paths](../../docs/development.md), build the plugin and run:

```sh
python3 tests/run_simulator.py tests/ribbon_simulator \
  --plugin src/mac/build/Release/plugin_geometry2d.dylib --timeout 180
bash tests/ribbon_native/run.sh
```

The project uses 640x480 content at 60 FPS. Keep the window unobscured. The runner
requires `SIMULATOR_TEST_EXIT: 0`; timeout or markerless exit is failure.
Pixel requests run on the main Lua state through an enterFrame listener.
Native bindings use mock display/memory interfaces and cannot prove engine
buffer acquisition, shader compilation or rendering.

## Coverage

- `contracts.lua`: default monotonic timestamps, future points, path-distance
  fade, reverse splits, reuse, style/no-op updates, capacity rebuilds and recovery.
  This suite does not establish `timestampMode="legacyCount"` compatibility.
- `geometry_compare.lua`: Lua/native position and oriented-triangle diagnostics
  for four shapes. The historical Lua connection rules differ from revised native
  joins; diagnostic differences must not be presented as topology equivalence.
- `pixels.lua`: first completed render, replacement, fade, width/color/alpha,
  scales .5/1/2, rotation and thin/wide ribbons.
- `benchmark.lua`: 32/128/512 trails, Lua no-AA, Lua outer-AA and native outer-AA;
  identical input/expiry/capacity workload, warmup and measurement windows.

Benchmark output includes FPS, frame/work P95, generation time, mesh creation,
draw calls, logical/submitted triangles, Lua memory delta and native vector capacity.
Generation/work measurements are CPU costs, not GPU time. Vector capacity excludes
allocator overhead and engine/GPU copies. Repeat with varied ordering before
making performance decisions; this workload is not a production FPS guarantee.

## AA comparison boundary

The Lua reference interpolates quantized vertex coverage over a local AA band.
Native AA evaluates distance with `fwidth()` and independently multiplies path
fade. Rotation, derivative footprint, corner interpolation and overlapping bands
can produce pixel differences; the suite reports them rather than claiming exact
equivalence. Reserve sufficient AA width for the minimum transform scale.
