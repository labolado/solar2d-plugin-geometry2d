LOCAL_PATH := $(call my-dir)

CORONA_NATIVE := /Applications/CoronaEnterprise
CORONA_ROOT := $(CORONA_NATIVE)/Corona
LUA_API_DIR := $(CORONA_ROOT)/shared/include/lua
LUA_API_CORONA := $(CORONA_ROOT)/shared/include/Corona

SRC_DIR := ../../shared
THIRD_PARTY := $(SRC_DIR)/../../third_party

POLYPARTITION_DIR := $(THIRD_PARTY)/polypartition/src
SOLAR2D_UTILS_DIR := $(THIRD_PARTY)/solar2d_native_utils
BYTEREADER_DIR := $(THIRD_PARTY)/ByteReader
EARCUT_DIR := $(THIRD_PARTY)/earcut/include

######################################################################

include $(CLEAR_VARS)
LOCAL_MODULE := liblua
LOCAL_SRC_FILES := ../corona-libs/jni/$(TARGET_ARCH_ABI)/liblua.so
LOCAL_EXPORT_C_INCLUDES := $(LUA_API_DIR)
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libcorona
LOCAL_SRC_FILES := ../corona-libs/jni/$(TARGET_ARCH_ABI)/libcorona.so
LOCAL_EXPORT_C_INCLUDES := $(LUA_API_CORONA)
include $(PREBUILT_SHARED_LIBRARY)

######################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := libplugin.geometry2d

LOCAL_C_INCLUDES := $(POLYPARTITION_DIR) \
    $(SOLAR2D_UTILS_DIR) \
    $(BYTEREADER_DIR) \
    $(EARCUT_DIR)

LOCAL_SRC_FILES := $(SRC_DIR)/plugin_geometry2d.cpp \
    $(SRC_DIR)/fringe.cpp \
    $(THIRD_PARTY)/polypartition/src/polypartition.cpp \
    $(THIRD_PARTY)/ByteReader/ByteReader.cpp

LOCAL_CFLAGS := \
    -DANDROID_NDK \
    -DNDEBUG \
    -D_REENTRANT \
    -DRtt_ANDROID_ENV \
    -DBR_NAMESPACE_PREFIX=geometry2d_br \
    -O3 \
    -fPIC \
    -DPIC

LOCAL_CPPFLAGS := -fexceptions -fPIC -std=c++14

LOCAL_LDFLAGS += -Wl,-s

# Export only luaopen_plugin_geometry2d; localize all other symbols.
LOCAL_LDFLAGS += -Wl,--version-script=$(LOCAL_PATH)/export.map

# 16 KB page alignment only applies to the 64-bit ABIs (Android 15+); the flag
# is meaningless for the 32-bit ABIs (armeabi-v7a, x86).
ifneq (,$(filter $(TARGET_ARCH_ABI),arm64-v8a x86_64))
    LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384
    LOCAL_LDFLAGS += -Wl,-z,common-page-size=16384
endif

LOCAL_SHARED_LIBRARIES := liblua libcorona

ifeq ($(TARGET_ARCH), arm)
    LOCAL_CFLAGS+= -D_ARM_ASSEM_
endif

ifeq ($(TARGET_ARCH), x86)
    LOCAL_DISABLE_FATAL_LINKER_WARNINGS := true
endif

LOCAL_ARM_MODE := arm
include $(BUILD_SHARED_LIBRARY)
