#!/bin/bash
#
# Builds the plugin.geometry2d Corona static library for iOS (device + simulator)
# and packages it into plugins/2025.3720/{iphone,iphone-sim}.
#
set -o errexit

export ZERO_AR_DATE=1

path=$(dirname "$0")
pushd "$path" > /dev/null; path=$(pwd); popd > /dev/null

TARGET_NAME=plugin_geometry2d
CONFIG=${CONFIG:-Release}
deployment_target=${IPHONEOS_DEPLOYMENT_TARGET:-15.0}
lib_version=${PLUGIN_BUILD:-2025.3720}
lib_name=lib${TARGET_NAME}.a   # libplugin_geometry2d.a

# Clean + build both SDKs.
xcodebuild -project "$path/Plugin.xcodeproj" -configuration "$CONFIG" \
    IPHONEOS_DEPLOYMENT_TARGET="$deployment_target" clean
xcodebuild -project "$path/Plugin.xcodeproj" -configuration "$CONFIG" \
    -sdk iphoneos IPHONEOS_DEPLOYMENT_TARGET="$deployment_target" ARCHS=arm64 ONLY_ACTIVE_ARCH=NO
xcodebuild -project "$path/Plugin.xcodeproj" -configuration "$CONFIG" \
    -sdk iphonesimulator IPHONEOS_DEPLOYMENT_TARGET="$deployment_target" ARCHS="x86_64 arm64" ONLY_ACTIVE_ARCH=NO

# $1 = plugin platform dir (iphone|iphone-sim)
# $2 = xcodebuild SDK build-dir suffix (iphoneos|iphonesimulator)
package() {
    local plat="$1" sdkdir="$2"
    local built="$path/build/$CONFIG-$sdkdir/${lib_name}"

    local dst_dir="$path/../../plugins/${lib_version}/${plat}"
    mkdir -p "$dst_dir"
    cp "$path/metadata.lua" "$dst_dir/metadata.lua"
    cp "$built" "$dst_dir/${lib_name}"

    echo "Packing ${plat}..."
    cd "$path"
    tar -czf "${lib_version}-${plat}.tgz" -C "$dst_dir" "${lib_name}" metadata.lua
    echo "  -> $dst_dir/${lib_name}"
    echo "  -> $path/${lib_version}-${plat}.tgz"
}

package iphone     iphoneos
package iphone-sim iphonesimulator

echo Done.
