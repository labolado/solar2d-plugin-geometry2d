#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
engine="$(python3 "$root/scripts/dev_config.py" --get coronaRoot "$@")"
lua_src="$engine/external/lua-5.1.3/src"
work="$(mktemp -d "${TMPDIR:-/tmp}/geometry2d-ribbon.XXXXXX")"
echo "Native test build: $work"
# Compile read-only engine Lua sources into a private temporary directory.
objects=()
for source in "$lua_src"/*.c; do
    name="$(basename "$source" .c)"
    case "$name" in lua|luac|print|noparser) continue ;; esac
    "${CC:-cc}" -O1 -g -fsanitize=address,undefined -I"$lua_src" -c "$source" -o "$work/$name.o"
    objects+=("$work/$name.o")
done
"${CXX:-c++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
    -I"$lua_src" -I"$engine/librtt/Corona" -I"$root/src/shared" \
    -I"$root/third_party/polypartition/src" \
    "$root/tests/ribbon_native/host.cpp" "$root/src/shared/ribbon_builder.cpp" \
    "$root/src/shared/ribbon_module.cpp" "${objects[@]}" -o "$work/ribbon-test"
cd "$root"
"$work/ribbon-test" tests/ribbon_native/main.lua
