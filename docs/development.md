# Development and tests

Copy `dev.example.json` to `dev.local.json` in the repository root and fill in
your machine paths. The local file is ignored; never force-add it. Python 3 is
required for the development runners. No third-party Python package is needed.

| JSON field | Environment override | Used by |
|---|---|---|
| coronaRoot | CORONA_ROOT | Native tests requiring engine sources; Simulator discovery |
| simulator | SOLAR2D_SIMULATOR | Simulator runner; accepts a macOS .app or executable |
| localPluginServerRoot | LOCAL_PLUGIN_SERVER_ROOT | Explicit local-server synchronization only |

Precedence: command-line override, environment, JSON, then portable defaults.
`--config FILE` selects another file; `GEOMETRY2D_DEV_CONFIG` also selects a file
for shell/native runners. JSON paths are relative to the config directory, while
command-line/environment paths are relative to the working directory. `~` is
expanded; shell substitutions and environment interpolation are not executed.
Unknown keys, malformed JSON, empty strings and wrong types are errors. Use
`null` for unused settings. Only settings required by the current operation are
resolved/checked. A missing default config is allowed if other inputs suffice.
CI does not read local JSON; explicit local-config selection in CI is rejected.

If `simulator` is omitted, the macOS Release Simulator under `coronaRoot` is used.
`CORONA_ROOT` here means an engine source checkout, not a Native SDK installation.
Platform build scripts retain their existing SDK/toolchain environment contracts
(including the Windows build script's existing CORONA_ROOT SDK meaning); they
do not require this development JSON or a private plugin server.

```sh
# Automated test: require an explicit result, enforce timeout, reap own process.
python3 tests/run_simulator.py tests/api_simulator --timeout 180

# Build and open visual examples; no test-marker deadline or server sync.
bash src/mac/start_simulator.sh
bash src/mac/start_simulator.sh --skip-build

# The same launcher can open any project interactively.
python3 tests/run_simulator.py examples/solar2d --interactive --local-plugin \
  --plugin src/mac/build/Release/plugin_geometry2d.dylib

# Native tests can use the shared local config.
bash tests/ribbon_native/run.sh
bash tests/earcut_backend_native/run.sh --config dev.local.json

# Separate opt-in operation; asks before overwriting server packages.
bash sync_local_plugins.sh mac-sim
```

`--plugin PATH` installs a dylib atomically before launch. Interactive mode stays
attached until the Simulator exits; Ctrl-C reaps only the process started by the
runner. Stop a previously running test before installing another plugin build.

With `--local-plugin --plugin PATH`, the runner instead copies a self-contained
project into a temporary directory, removes plugin download declarations in that
copy, and preloads the specified dylib directly. The source build.settings and
global plugin installation are untouched by this mode. The temporary project is
removed after the child exits. Symlinks are rejected; projects importing files
outside their project directory must use the ordinary runner instead. Resource
paths/sandbox identity refer to the temporary project, not the original directory.
`start_simulator.sh` uses this local mode; its optional build step retains the
existing build.sh installation behavior.

The public example pins the planned `v1` release. Those download URLs are not
usable until you publish it; use local mode while developing. See [releasing](releasing.md).

## Source and generated files

### Apple deployment targets

Project Debug/Release settings and build scripts default to macOS **12.0**,
iOS **15.0**, and tvOS **15.0**, including simulator slices. These are fixed
release baselines, not values automatically raised to match the installed SDK.
CI uses the same script defaults. New binaries do not promise support below
these versions; raising the target does not itself establish runtime coverage.

Explicit environment overrides are passed as Xcode command-line settings to both
clean and build, overriding the project settings:

```sh
MACOSX_DEPLOYMENT_TARGET=12.0 bash src/mac/build.sh
IPHONEOS_DEPLOYMENT_TARGET=15.0 bash src/ios/build.sh
TVOS_DEPLOYMENT_TARGET=15.0 bash src/tvos/build.sh
```

An override must be supported by the selected Xcode/SDK. Supporting older systems
requires an appropriate toolchain and actual compatibility tests, not suppressing
deployment-target validation. Device builds retain arm64; macOS and mobile
simulator builds retain arm64 + x86_64. No public Lua API or plugin ABI identifier
changes with this deployment-policy update.

### Generated artifacts

`plugins/` is generated, not a source dependency. Build scripts recreate it;
metadata sources stay in `src/`. CI builds all artifacts and packages Actions
outputs, including all four Android ABIs. Releases distribute binaries.
Branch pushes, PRs and `workflow_dispatch` build without publishing. Only an
explicit version-tag push can publish; VERSION, tag and the example pin must
agree. Test CI changes manually before creating a release tag.

Raw logs, crash dumps, local configs and temporary exports stay local. Commit
only reviewed summaries containing versions, metrics, failures and limitations.
Do not paste raw paths into those summaries. Business-derived geometry needs
explicit publication approval even when it contains no names or paths.

Run `python3 scripts/check_public_tree.py` before adding/pushing files. It checks
tracked and nonignored candidate files, not Git history, and is not a complete
credential scanner. History review and any credential rotation remain separate.
