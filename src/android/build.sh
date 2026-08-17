#!/bin/bash

# This option is used to exit the script as
# soon as a command returns a non-zero value.
set -o errexit

path=`dirname $0`

TARGET_NAME=geometry2d
CONFIG=Release
DEVICE_TYPE=all
BUILD_TYPE=clean

CPU_CORES=$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
echo "CPU_CORES: ${CPU_CORES}"

#
# Checks exit value for error
#
ANDROID_SDK_HOME=${ANDROID_SDK_HOME:-/opt/homebrew/share/android-commandlinetools}
# Use the newest installed NDK (r27+ required: older llvm tools are x86_64-only
# and crash under Rosetta on recent macOS).
if [ -z "$ANDROID_NDK" ]
then
	ANDROID_NDK=${ANDROID_SDK_HOME}/ndk/$(ls -1 ${ANDROID_SDK_HOME}/ndk | sort -V | tail -1)
fi
# if [ -z "$ANDROID_NDK" ]
# then
# 	echo "ERROR: ANDROID_NDK environment variable must be defined"
# 	exit 0
# fi

# Canonicalize paths
pushd $path > /dev/null
dir=`pwd`
path=$dir
popd > /dev/null

######################
# Build .so          #
######################

pushd $path/jni > /dev/null

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
	rm -rf $path/obj/ $path/libs/
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

	$ANDROID_NDK/ndk-build $FLAGS V=1 APP_OPTIM=$OPTIM_FLAGS -j${CPU_CORES}
else
	echo "----------------------------------------------------------------------------"
	echo "$ANDROID_NDK/ndk-build $FLAGS V=1 MY_CFLAGS="$CFLAGS" APP_OPTIM=$OPTIM_FLAGS -j${CPU_CORES}"
	echo "----------------------------------------------------------------------------"

	$ANDROID_NDK/ndk-build $FLAGS V=1 MY_CFLAGS="$CFLAGS" APP_OPTIM=$OPTIM_FLAGS -j${CPU_CORES}
fi

find "$path/libs" \( -name liblua.so -or -name libcorona.so -or -name libopenal.so \)  -delete
echo "$path/libs"
rm -rf "$path/jniLibs"
mv "$path/libs" "$path/jniLibs"

popd > /dev/null

######################
# Post-compile Steps #
######################

echo Done.

dst_dir=$path/../../plugins/2025.3720/android
lib_name=libplugin.${TARGET_NAME}.so

copy_file() {
	if [ ! -d ${dst_dir} ]
	then
		mkdir -p ${dst_dir}
	fi
	local_dst_dir=${dst_dir}/jniLibs/${1}
	if [ ! -d ${local_dst_dir} ]
	then
		mkdir -p ${local_dst_dir}
	fi
	cp ${path}/jniLibs/${1}/${lib_name} ${local_dst_dir}/${lib_name}
}

copy_file arm64-v8a
copy_file armeabi-v7a
copy_file x86
copy_file x86_64
cp $path/metadata.lua ${dst_dir}/metadata.lua
cp ${path}/jniLibs/armeabi-v7a/${lib_name} ${dst_dir}/${lib_name}

echo Packing binaries...
tar -czvf data.tgz -C $path jniLibs -C $path/jniLibs/armeabi-v7a ${lib_name} -C $path metadata.lua
echo $path/data.tgz.
