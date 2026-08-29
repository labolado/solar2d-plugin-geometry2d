#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]
then
    echo "Usage: $0 SOLAR2D_SOURCE [CORONA_NATIVE]" >&2
    exit 2
fi

source_root=$1
native_root=${2:-/Applications/CoronaEnterprise}
corona_headers="$native_root/Corona/shared/include/Corona"
lua_headers="$native_root/Corona/shared/include/lua"

if [ ! -f "$source_root/librtt/Corona/CoronaLua.h" ] || \
   [ ! -f "$source_root/external/lua-5.1.3/src/lua.h" ]
then
    echo "ERROR: $source_root is not a complete Solar2D source checkout." >&2
    exit 1
fi

mkdir -p "$corona_headers" "$lua_headers"
cp "$source_root"/librtt/Corona/*.h "$corona_headers/"
cp "$source_root/external/lua-5.1.3/src/lua.h" \
   "$source_root/external/lua-5.1.3/src/lauxlib.h" \
   "$source_root/external/lua-5.1.3/src/luaconf.h" \
   "$source_root/external/lua-5.1.3/src/lualib.h" \
   "$lua_headers/"

echo "Solar2D native headers installed under $native_root"
