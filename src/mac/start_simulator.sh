#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
skip_build=0
runner_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build|-n) skip_build=1; shift ;;
        --config|--simulator)
            [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
            runner_args+=("$1" "$2"); shift 2 ;;
        --help|-h)
            echo 'Usage: start_simulator.sh [--skip-build] [--config FILE] [--simulator PATH]'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
if [[ "$skip_build" -eq 0 ]]; then
    (cd "$root/src/mac" && bash build.sh)
fi
exec python3 "$root/tests/run_simulator.py" "$root/examples/solar2d" \
    --interactive --local-plugin --plugin "$root/src/mac/build/${CONFIG:-Release}/plugin_geometry2d.dylib" ${runner_args[@]+"${runner_args[@]}"}
