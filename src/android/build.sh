#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

path=$(dirname "$0")

TARGET_NAME=geometry2d
CONFIG=${CONFIG:-Release}
DEVICE_TYPE=all
BUILD_TYPE=clean
PLUGIN_BUILD=${PLUGIN_BUILD:-2025.3720}
CORONA_NATIVE=${CORONA_NATIVE:-/Applications/CoronaEnterprise}
CORONA_AAR=${CORONA_AAR:-$CORONA_NATIVE/Corona/android/lib/gradle/Corona.aar}

CPU_CORES=$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
echo "CPU_CORES: ${CPU_CORES}"

#
# Checks exit value for error
#
ANDROID_SDK_HOME=${ANDROID_SDK_HOME:-/opt/homebrew/share/android-commandlinetools}
# Use the newest installed NDK (r27+ required: older llvm tools are x86_64-only
# and crash under Rosetta on recent macOS).
if [ -z "${ANDROID_NDK:-}" ]
then
	if [ ! -d "$ANDROID_SDK_HOME/ndk" ]
	then
		echo "ERROR: ANDROID_NDK is unset and $ANDROID_SDK_HOME/ndk does not exist." >&2
		exit 1
	fi
	ANDROID_NDK=$ANDROID_SDK_HOME/ndk/$(ls -1 "$ANDROID_SDK_HOME/ndk" | sort -V | tail -1)
fi

if [ ! -x "$ANDROID_NDK/ndk-build" ]
then
	echo "ERROR: ndk-build was not found under ANDROID_NDK=$ANDROID_NDK." >&2
	exit 1
fi

# Canonicalize paths
pushd "$path" > /dev/null
dir=$(pwd)
path=$dir
popd > /dev/null

# Refresh the Solar2D prebuilt libraries when Corona.aar is available. A local
# cached corona-libs tree remains supported for offline development.
if [ -f "$CORONA_AAR" ]
then
	mkdir -p "$path/corona-libs"
	unzip -oq "$CORONA_AAR" "jni/*/liblua.so" "jni/*/libcorona.so" -d "$path/corona-libs"
fi

for abi in armeabi-v7a arm64-v8a x86 x86_64
do
	for library in liblua.so libcorona.so
	do
		if [ ! -f "$path/corona-libs/jni/$abi/$library" ]
		then
			echo "ERROR: missing corona-libs/jni/$abi/$library; provide CORONA_AAR or a cached corona-libs tree." >&2
			exit 1
		fi
	done
done

######################
# Build .so          #
######################

pushd "$path/jni" > /dev/null

if [ "Release" == "$CONFIG" ]
then
	echo "Building RELEASE"
	OPTIM_FLAGS="release"
else
	echo "Building DEBUG"
	OPTIM_FLAGS="debug"
fi

if [ "clean" == "$BUILD_TYPE" ]
then
	echo "== Clean build =="
	rm -rf "$path/obj" "$path/libs"
	FLAGS="-B"
else
	echo "== Incremental build =="
	FLAGS=""
fi

CFLAGS=

if [ "$OPTIM_FLAGS" = "debug" ]
then
	CFLAGS="${CFLAGS} -DRtt_DEBUG -g"
	FLAGS="$FLAGS NDK_DEBUG=1"
fi

if [ -z "$CFLAGS" ]
then
	echo "----------------------------------------------------------------------------"
	echo "$ANDROID_NDK/ndk-build $FLAGS V=1 APP_OPTIM=$OPTIM_FLAGS -j${CPU_CORES}"
	echo "----------------------------------------------------------------------------"

	"$ANDROID_NDK/ndk-build" $FLAGS V=1 APP_OPTIM=$OPTIM_FLAGS \
		CORONA_NATIVE="$CORONA_NATIVE" -j"$CPU_CORES"
else
	echo "----------------------------------------------------------------------------"
	echo "$ANDROID_NDK/ndk-build $FLAGS V=1 MY_CFLAGS="$CFLAGS" APP_OPTIM=$OPTIM_FLAGS -j${CPU_CORES}"
	echo "----------------------------------------------------------------------------"

	"$ANDROID_NDK/ndk-build" $FLAGS V=1 MY_CFLAGS="$CFLAGS" APP_OPTIM=$OPTIM_FLAGS \
		CORONA_NATIVE="$CORONA_NATIVE" -j"$CPU_CORES"
fi

find "$path/libs" \( -name liblua.so -o -name libcorona.so -o -name libopenal.so \) -delete
echo "$path/libs"
rm -rf "$path/jniLibs"
mv "$path/libs" "$path/jniLibs"

popd > /dev/null

######################
# Post-compile Steps #
######################

echo Done.

dst_dir="$path/../../plugins/$PLUGIN_BUILD/android"
lib_name=libplugin.${TARGET_NAME}.so

copy_file() {
	local abi=$1
	local local_dst_dir="$dst_dir/jniLibs/$abi"
	mkdir -p "$local_dst_dir"
	cp "$path/jniLibs/$abi/$lib_name" "$local_dst_dir/$lib_name"
}

copy_file arm64-v8a
copy_file armeabi-v7a
copy_file x86
copy_file x86_64
cp "$path/metadata.lua" "$dst_dir/metadata.lua"
cp "$path/jniLibs/armeabi-v7a/$lib_name" "$dst_dir/$lib_name"

for abi in armeabi-v7a arm64-v8a x86 x86_64
do
	test -f "$dst_dir/jniLibs/$abi/$lib_name"
done

# echo Packing binaries...
# tar -czvf data.tgz -C "$path" jniLibs -C "$path/jniLibs/armeabi-v7a" "$lib_name" -C "$path" metadata.lua
# echo $path/data.tgz.
