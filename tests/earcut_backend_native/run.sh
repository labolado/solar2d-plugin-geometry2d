#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
engine="$(python3 "$root/scripts/dev_config.py" --get coronaRoot "$@")"
work="$(mktemp -d "${TMPDIR:-/tmp}/geometry2d-earcut-backend.XXXXXX")"
cd "$root"
"${CXX:-c++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
    -I "$engine/external/lua-5.1.3/src" -I "$engine/librtt/Corona" \
    -I src/shared -I third_party/polypartition/src -I third_party/earcut/include \
    -I third_party/clipper2/CPP/Clipper2Lib/include \
    tests/earcut_backend_native/main.cpp src/shared/mesh_builder.cpp \
    src/shared/sdf_builder.cpp src/shared/earcut_stroke_builder.cpp src/shared/fringe.cpp \
    third_party/clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp -o "$work/test"
"$work/test"
"${CXX:-c++}" -std=c++17 -O2 -I src/shared -I third_party/earcut/include \
    -I third_party/clipper2/CPP/Clipper2Lib/include tests/earcut_backend_native/benchmark.cpp \
    src/shared/earcut_stroke_builder.cpp src/shared/sdf_builder.cpp \
    third_party/clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp -o "$work/bench"
"$work/bench"
