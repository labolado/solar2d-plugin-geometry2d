#!/bin/bash
#
# Builds the plugin.geometry2d tvOS plugin as a Corona dynamic framework for device
# (appletvos) and simulator (appletvsimulator), combines them into
# Corona_plugin_geometry2d.xcframework, and packages it into
# plugins/2025.3720/appletvos.
#
set -o errexit

path=$(dirname "$0")
pushd "$path" > /dev/null; path=$(pwd); popd > /dev/null

CONFIG=Release
FW=Corona_plugin_geometry2d
lib_version=2025.3720

xcodebuild -project "$path/Plugin.xcodeproj" -configuration $CONFIG clean
xcodebuild -project "$path/Plugin.xcodeproj" -configuration $CONFIG -sdk appletvos
xcodebuild -project "$path/Plugin.xcodeproj" -configuration $CONFIG -sdk appletvsimulator

DEV_FW="$path/build/$CONFIG-appletvos/${FW}.framework"
SIM_FW="$path/build/$CONFIG-appletvsimulator/${FW}.framework"

XCFW="$path/build/${FW}.xcframework"
rm -rf "$XCFW"
xcodebuild -create-xcframework \
    -framework "$DEV_FW" \
    -framework "$SIM_FW" \
    -output "$XCFW"

dst_dir="$path/../../plugins/${lib_version}/appletvos"
mkdir -p "$dst_dir"
rm -rf "$dst_dir/${FW}.xcframework"
cp -R "$XCFW" "$dst_dir/"

echo "Packing appletvos..."
cd "$path"
tar -czf "${lib_version}-appletvos.tgz" -C "$dst_dir" "${FW}.xcframework"
echo "  -> $dst_dir/${FW}.xcframework"
echo "  -> $path/${lib_version}-appletvos.tgz"
echo Done.
