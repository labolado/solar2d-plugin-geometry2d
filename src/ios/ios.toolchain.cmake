# =================================================================
# CMake Toolchain file for iOS
#
#
# Usage:
#   cmake .. -G "Unix Makefiles" \
#            -DCMAKE_TOOLCHAIN_FILE=path/to/ios.toolchain.cmake \
#            -DIOS_PLATFORM=[OS64, SIMULATORARM64, SIMULATOR64] \
#            -DIOS_DEPLOYMENT_TARGET=12.0
#
# =================================================================

# 设置目标系统为 iOS
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_CROSSCOMPILING TRUE)

# 检查平台参数
if(NOT DEFINED IOS_PLATFORM)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(IOS_PLATFORM "SIMULATORARM64") # Apple Silicon Mac 默认模拟器
  else()
    set(IOS_PLATFORM "SIMULATOR64")    # Intel Mac 默认模拟器
  endif()
  message(STATUS "IOS_PLATFORM is not set. Defaulting to ${IOS_PLATFORM}")
endif()

# 设置最低部署版本 (可以根据你的项目需求修改)
if(NOT DEFINED IOS_DEPLOYMENT_TARGET)
  set(IOS_DEPLOYMENT_TARGET "12.0")
endif()
message(STATUS "Building for iOS Deployment Target: ${IOS_DEPLOYMENT_TARGET}")


# 查找 SDK
if(IOS_PLATFORM STREQUAL "OS64")
  set(IOS_SDK_NAME "iphoneos")
elseif(IOS_PLATFORM STREQUAL "SIMULATOR64" OR IOS_PLATFORM STREQUAL "SIMULATORARM64")
  set(IOS_SDK_NAME "iphonesimulator")
else()
  message(FATAL_ERROR "Unsupported IOS_PLATFORM value: ${IOS_PLATFORM}. Supported values are OS64, SIMULATOR64, SIMULATORARM64.")
endif()

find_program(XCRUN_EXECUTABLE xcrun)
execute_process(
  COMMAND ${XCRUN_EXECUTABLE} --sdk ${IOS_SDK_NAME} --show-sdk-path
  OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Using iOS SDK at: ${CMAKE_OSX_SYSROOT}")

# 设置编译器
execute_process(
  COMMAND ${XCRUN_EXECUTABLE} --find clang
  OUTPUT_VARIABLE CMAKE_C_COMPILER
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND ${XCRUN_EXECUTABLE} --find clang++
  OUTPUT_VARIABLE CMAKE_CXX_COMPILER
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Using C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Using C++ compiler: ${CMAKE_CXX_COMPILER}")

# 设置架构和系统处理器
if(IOS_PLATFORM STREQUAL "OS64")
  set(CMAKE_OSX_ARCHITECTURES "arm64")
  set(CMAKE_SYSTEM_PROCESSOR "arm64")
elseif(IOS_PLATFORM STREQUAL "SIMULATOR64")
  set(CMAKE_OSX_ARCHITECTURES "x86_64")
  set(CMAKE_SYSTEM_PROCESSOR "x86_64")
elseif(IOS_PLATFORM STREQUAL "SIMULATORARM64")
  set(CMAKE_OSX_ARCHITECTURES "arm64")
  set(CMAKE_SYSTEM_PROCESSOR "arm64")
endif()

message(STATUS "Building for architectures: ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")

# 编译器标志
if(IOS_PLATFORM STREQUAL "OS64")
  set(ARCH_FLAGS "-arch arm64")
elseif(IOS_PLATFORM STREQUAL "SIMULATOR64")
  set(ARCH_FLAGS "-arch x86_64")
elseif(IOS_PLATFORM STREQUAL "SIMULATORARM64")
  set(ARCH_FLAGS "-arch arm64")
endif()

set(COMMON_FLAGS "${ARCH_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT} -miphoneos-version-min=${IOS_DEPLOYMENT_TARGET}")
set(CMAKE_C_FLAGS "${COMMON_FLAGS} ${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags")
set(CMAKE_CXX_FLAGS "${COMMON_FLAGS} ${CMAKE_CXX_FLAGS}" CACHE STRING "C++ compiler flags")

# 设置链接器标志
set(CMAKE_EXE_LINKER_FLAGS "${ARCH_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT} -miphoneos-version-min=${IOS_DEPLOYMENT_TARGET}" CACHE STRING "Linker flags")
set(CMAKE_SHARED_LINKER_FLAGS "${ARCH_FLAGS} -isysroot ${CMAKE_OSX_SYSROOT} -miphoneos-version-min=${IOS_DEPLOYMENT_TARGET}" CACHE STRING "Shared linker flags")

# 设置 CMake 查找行为
set(CMAKE_FIND_ROOT_PATH ${CMAKE_OSX_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 跳过编译器测试（因为是交叉编译）
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)
set(CMAKE_C_ABI_COMPILED TRUE)
set(CMAKE_CXX_ABI_COMPILED TRUE)
