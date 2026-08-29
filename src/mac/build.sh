#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

path=$(dirname "$0")

OUTPUT_DIR=${1:-.}
TARGET_NAME=plugin_geometry2d
OUTPUT_SUFFIX=dylib
CONFIG=${CONFIG:-Release}
PLUGIN_BUILD=${PLUGIN_BUILD:-2025.3720}

#
# Canonicalize relative paths to absolute paths
#
pushd "$path" > /dev/null
dir=$(pwd)
path=$dir
popd > /dev/null

mkdir -p "$OUTPUT_DIR"
pushd "$OUTPUT_DIR" > /dev/null
dir=$(pwd)
OUTPUT_DIR=$dir
popd > /dev/null

echo "OUTPUT_DIR: $OUTPUT_DIR"

# Clean.
xcodebuild -project "$path/Plugin.xcodeproj" -configuration "$CONFIG" clean

# Build Mac.
xcodebuild -project "$path/Plugin.xcodeproj" -configuration "$CONFIG" \
    ARCHS="x86_64 arm64" ONLY_ACTIVE_ARCH=NO

lib_name=$TARGET_NAME.$OUTPUT_SUFFIX

# Copy to destination.
cp "$path/build/$CONFIG/${lib_name}" "$OUTPUT_DIR"
echo "$OUTPUT_DIR"/${lib_name}

PLUGINS_DIR="$HOME/Library/Application Support/Corona/Simulator/Plugins"
mkdir -p "$PLUGINS_DIR"
cp "$path/build/$CONFIG/${lib_name}" "${PLUGINS_DIR}"
echo ${PLUGINS_DIR}/${lib_name}

dst_dir="$path/../../plugins/$PLUGIN_BUILD/mac-sim"
mkdir -p "$dst_dir"
cp "$path/build/$CONFIG/${lib_name}" "$dst_dir"

echo "Packing binaries..."
tar -czvf data.tgz -C "$dst_dir" "$lib_name"
echo $path/data.tgz.

echo Done.
