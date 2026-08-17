#!/bin/bash

# This option is used to exit the script as
# soon as a command returns a non-zero value.
set -o errexit

path=`dirname $0`

OUTPUT_DIR=$1
TARGET_NAME=plugin_geometry2d
OUTPUT_SUFFIX=dylib
CONFIG=Release

#
# Canonicalize relative paths to absolute paths
#
pushd $path > /dev/null
dir=`pwd`
path=$dir
popd > /dev/null

if [ -z "$OUTPUT_DIR" ]
then
    OUTPUT_DIR=.
fi

pushd $OUTPUT_DIR > /dev/null
dir=`pwd`
OUTPUT_DIR=$dir
popd > /dev/null

echo "OUTPUT_DIR: $OUTPUT_DIR"

# Clean.
xcodebuild -project "$path/Plugin.xcodeproj" -configuration $CONFIG clean

# Build Mac.
xcodebuild -project "$path/Plugin.xcodeproj" -configuration $CONFIG

lib_name=$TARGET_NAME.$OUTPUT_SUFFIX

# Copy to destination.
cp "$path/build/$CONFIG/${lib_name}" "$OUTPUT_DIR"
echo "$OUTPUT_DIR"/${lib_name}

PLUGINS_DIR="$HOME/Library/Application Support/Corona/Simulator/Plugins/"
cp "$path/build/$CONFIG/${lib_name}" "${PLUGINS_DIR}"
echo ${PLUGINS_DIR}/${lib_name}

dst_dir=${path}/../../plugins/2025.3720/mac-sim/
if [ ! -d ${dst_dir} ]
then
    mkdir -p ${dst_dir}
fi
cp "$path/build/$CONFIG/${lib_name}" "${dst_dir}"

echo "Packing binaries..."
tar -czvf data.tgz -C $dst_dir ${lib_name}
echo $path/data.tgz.

echo Done.
