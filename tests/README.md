# Test suites

Configure machine paths using [development configuration](../docs/development.md).
The [API reference](../docs/api.md) defines current behavior. Run commands below
from the repository root; build the macOS plugin before Simulator tests.
Use `--plugin src/mac/build/Release/plugin_geometry2d.dylib` to install that
explicit binary, or omit it to reuse the installed plugin.

| Suite | Purpose | Run |
|---|---|---|
| api_simulator | Public API/errors, output modes, retained views and packed attributes/colors | `python3 tests/run_simulator.py tests/api_simulator` |
| sdf_simulator | Distance, fill, shader/UV/transform pixels and rendering benchmarks | [Instructions](sdf_simulator/README.md) |
| sdf_native | Partition geometry, distance/coverage, sanitizers and native benchmarks | `bash tests/sdf_native/run.sh` |
| earcut_backend_native | Fill/local-distance geometry, capacity, coverage and native benchmarks | `bash tests/earcut_backend_native/run.sh` |
| cutout_corner | Low-level fringe reflex/hole regression and overlap limits | [Instructions](cutout_corner/README.md) |
| ribbon_simulator | Stateful ribbon contracts, pixels and load tests | [Instructions](ribbon_simulator/README.md) |
| ribbon_native | Native/mock binding lifecycle; not rendering proof | `bash tests/ribbon_native/run.sh` |
| tooling | Configuration, launcher and release-policy checks | `python3 -m unittest discover -s tests/tooling -v` |
| local_plugin_simulator | Isolated local binary loading, without release downloads/global installation | Command below |

```sh
python3 tests/run_simulator.py tests/local_plugin_simulator --local-plugin \
  --plugin src/mac/build/Release/plugin_geometry2d.dylib
```

The API suite loads the example assertion files instead of duplicating them.
Visual examples require observation; they are not automatic assertions.
Native internal geometry enums are algorithm fixtures, not public Lua aliases.

## Experiments and validation boundaries

[Inner-stroke prototypes](inner_stroke_prototypes/README.md) remain active,
isolated experiments, not stable API or production replacements. Their README
documents reproduction, fixture requirements and incomplete acceptance gates.
Dropping failed triangles or applying unverified local repair is not an accepted
fallback; production geometry failures must remain explicit.

Simulator passes require explicit result markers. Diagnostic completion does
not mean every experimental geometry succeeded. Missing optional private fixtures
must report SKIP, not PASS. CPU timing is not GPU timing or device performance.
Keep generated reports/logs in ignored `results/` directories or `.local/`;
document reusable procedures and current limitations rather than dated run history.
