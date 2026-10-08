# plugin.geometry2d

Solar2D native geometry plugin. See [API](docs/api.md),
[development configuration](docs/development.md), and [test suites](tests/README.md).

Copy `dev.example.json` to the ignored `dev.local.json` for machine-specific
engine, Simulator and optional local plugin server paths. Generated `plugins/`
artifacts are not tracked; platform build scripts and CI recreate them.

Releases use explicit version tags, not CI run numbers. See
[release procedure](docs/releasing.md). The example pins the planned `v1`;
until it is published, use the local launcher below instead of release downloads.

```sh
git submodule update --init --recursive
bash src/mac/start_simulator.sh
python3 tests/run_simulator.py tests/api_simulator
```
