#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$(mktemp -d "${TMPDIR:-/tmp}/geometry2d-sdf.XXXXXX")"
cd "$root"
common=(-std=c++17 -I src/shared -I third_party/earcut/include -I third_party/clipper2/CPP/Clipper2Lib/include)
sources=(src/shared/sdf_builder.cpp third_party/clipper2/CPP/Clipper2Lib/src/clipper.engine.cpp)
"${CXX:-c++}" "${common[@]}" -O1 -g -fsanitize=address,undefined tests/sdf_native/main.cpp "${sources[@]}" -o "$work/test"
"$work/test"
"${CXX:-c++}" "${common[@]}" -O2 tests/sdf_native/benchmark.cpp "${sources[@]}" src/shared/fringe.cpp -o "$work/bench"
"$work/bench"
