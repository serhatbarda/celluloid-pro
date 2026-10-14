# Reafilm Pro - top-level build (SPEC §11).
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release [-DFETCHCONTENT_SOURCE_DIR_OPENFX=external-openfx-ref]
#   cmake --build build -j
#   -> build/ReafilmPro.ofx.bundle   (Contents/{MacOS|Win64|Linux-x86-64}/ReafilmPro.ofx, Info.plist, Resources/)
#
# Targets: celcore (colour science), celtex (texture + frame engine + CPU backend), celgpu
# (OpenCL/CUDA/Metal backends, from src/gpu), cellic (licensing), cel_ofxsupport (OpenFX C++
# Support library, static, from the pinned OpenFX sources) and reafilm_ofx (the plug-in module).
#
# Default = PERSONAL build (no licensing); -DCEL_LICENSING=ON = licensed build (SPEC §9).
cmake_minimum_required(VERSION 3.20)

# ---- platform defaults that must be set before project() -----------------------------------
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
  option(CEL_UNIVERSAL "Build a universal (arm64 + x86_64) macOS binary" ON)
  if(CEL_UNIVERSAL AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
  endif()
endif()
# Static MSVC runtime (/MT, /MTd): the plug-in DLL must not depend on a VC++ redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime")

project(ReafilmPro VERSION 1.3.0 LANGUAGES C CXX)
# Languages must be enabled in the top-level directory: CMake generates the per-language rules
# (e.g. CMAKE_OBJCXX_COMPILE_OBJECT) only for languages enabled here, so an enable_language()
# inside src/gpu or tests/gpu alone breaks the generate step on macOS.
if(APPLE)
  enable_language(OBJCXX)   # Metal backend (src/gpu/metal/*.mm) and its test environment
endif()

set(CEL_PRODUCT_NAME "Reafilm Pro")
set(CEL_PLUGIN_IDENTIFIER "de.serhatbarda.CelluloidPro")
set(CEL_BUNDLE_NAME "ReafilmPro.ofx.bundle")
set(CEL_BUNDLE_EXECUTABLE "ReafilmPro.ofx")
set(CEL_COPYRIGHT "Copyright (c) 2026 Serhat (serhatbarda.de). All rights reserved.")
set(CEL_MACOS_MIN "11.0")
set(CEL_BUNDLE_DIR "${CMAKE_BINARY_DIR}/${CEL_BUNDLE_NAME}")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---- options ----------------------------------------------------------------------------------
option(CEL_BUILD_TESTS "Build the unit tests (tests/*) and register them with CTest" ON)
option(CEL_BUILD_TOOLS "Build the developer tools (tools/licensegen, tools/ofxhost on Linux)" ON)
option(CEL_ENABLE_OPENCL "OpenCL GPU backend (runtime loaded, Khronos headers)" ON)
option(CEL_ENABLE_CUDA "CUDA GPU backend (needs the CUDA toolkit / nvcc)" OFF)
if(APPLE)
  option(CEL_ENABLE_METAL "Metal GPU backend (Apple only)" ON)
else()
  set(CEL_ENABLE_METAL OFF CACHE BOOL "Metal GPU backend (Apple only)" FORCE)
endif()
option(CEL_DEV_LICENSE "Accept the DEVELOPMENT license key pair (tests only, never for releases)" OFF)
# Personal build (default, SPEC §9/§13): no license check, no trial watermark, LUT export always
# available; the License group is kept (hidden) so saved projects stay compatible.
# -DCEL_LICENSING=ON builds the licensed (commercial) variant with keys, trial watermark and the
# LUT-export gate. An existing build dir configured with CEL_DEV_LICENSE=ON (only meaningful for a
# licensed build) is switched to the licensed variant on first configure.
if(CEL_DEV_LICENSE AND NOT DEFINED CACHE{CEL_LICENSING})
  message(WARNING "Reafilm: CEL_DEV_LICENSE=ON implies the licensed build - setting CEL_LICENSING=ON")
  set(CEL_LICENSING ON CACHE BOOL "Licensed build (license keys, trial watermark, LUT-export gate); OFF = personal build")
endif()
option(CEL_LICENSING "Licensed build (license keys, trial watermark, LUT-export gate); OFF = personal build" OFF)
if(CEL_DEV_LICENSE AND NOT CEL_LICENSING)
  message(FATAL_ERROR "CEL_DEV_LICENSE=ON needs the licensed build: configure with -DCEL_LICENSING=ON "
                      "(the personal build has no license check at all)")
endif()
option(CEL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in our own code" OFF)
set(CEL_BUILD_ID "" CACHE STRING "Build identifier shown in the About group (default: git commit)")

if(CEL_ENABLE_METAL AND NOT APPLE)
  message(FATAL_ERROR "CEL_ENABLE_METAL requires an Apple platform")
endif()

if(CEL_BUILD_TESTS)
  enable_testing()     # before any add_subdirectory() so every directory can register tests
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)        # static libs are linked into a loadable module
set(CMAKE_CXX_VISIBILITY_PRESET hidden)        # export only the two OFX entry points
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)     # Metal backend (.mm)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(CEL_ENABLE_CUDA)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    # Maxwell (sm_52) .. Hopper (sm_90) SASS + sm_90 PTX for forward compatibility (SPEC §8).
    set(CMAKE_CUDA_ARCHITECTURES "52-real;60-real;61-real;70-real;75-real;80-real;86-real;89-real;90-real;90-virtual"
        CACHE STRING "CUDA architectures")
  endif()
  if(MSVC)
    set(CMAKE_CUDA_RUNTIME_LIBRARY Static CACHE STRING "CUDA runtime library")
  endif()
  enable_language(CUDA)
endif()

include(CelHelpers)       # cel_set_warnings(), cel_collect_sources()
include(CelVersion)       # CEL_BUILD_ID / CEL_BUILD_DATE
include(CelOpenFX)        # OpenFX sources (pinned) + cel_ofxsupport

set(CEL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(MSVC)
  # UTF-8 sources for every C/C++ target (tests and tools included); never passed to nvcc.
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
endif()

# Common include interface for all libraries: "core/Core.h", "../kernels/..." style includes.
add_library(cel_includes INTERFACE)
target_include_directories(cel_includes INTERFACE "${CEL_SRC}" "${CEL_SRC}/kernels")
target_compile_features(cel_includes INTERFACE cxx_std_17)
if(MSVC)
  # UTF-8 sources, standard conformance, no min/max macros, no CRT "secure" deprecation noise
  target_compile_options(cel_includes INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8;/permissive-;/Zc:__cplusplus>")
  target_compile_definitions(cel_includes INTERFACE NOMINMAX _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
endif()

# Each component may bring its own CMakeLists.txt (src/<component>/CMakeLists.txt, which must
# define the documented target); otherwise the library is defined here from the component's
# sources. Our warning flags are applied in both cases.
find_package(Threads REQUIRED)

# ---- celcore: colour science (src/core) -----------------------------------------------------------
if(EXISTS "${CEL_SRC}/core/CMakeLists.txt")
  add_subdirectory(src/core)
else()
  cel_collect_sources(CEL_CORE_SOURCES COMPONENT core DIRS "${CEL_SRC}/core")
  add_library(celcore STATIC ${CEL_CORE_SOURCES})
endif()
cel_require_target(celcore)
target_link_libraries(celcore PUBLIC cel_includes Threads::Threads)   # spectral bakes use std::thread

# ---- celtex: texture passes, plan builder, CPU backend (src/texture + src/engine) -------------------
if(EXISTS "${CEL_SRC}/engine/CMakeLists.txt")
  add_subdirectory(src/engine)
elseif(EXISTS "${CEL_SRC}/texture/CMakeLists.txt")
  add_subdirectory(src/texture)
else()
  cel_collect_sources(CEL_TEX_SOURCES COMPONENT engine DIRS "${CEL_SRC}/texture" "${CEL_SRC}/engine")
  add_library(celtex STATIC ${CEL_TEX_SOURCES})
endif()
cel_require_target(celtex)
target_link_libraries(celtex PUBLIC celcore)

# ---- cellic: licensing (src/license + vendored Monocypher) -----------------------------------------
if(EXISTS "${CEL_SRC}/license/CMakeLists.txt")
  add_subdirectory(src/license)          # cellic, cellic_sign, cel_monocypher
else()
  cel_collect_sources(CEL_LIC_SOURCES COMPONENT license DIRS "${CEL_SRC}/license")
  add_library(cellic STATIC ${CEL_LIC_SOURCES})
  set(CEL_MONOCYPHER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher")
  add_library(cel_monocypher STATIC "${CEL_MONOCYPHER_DIR}/monocypher.c" "${CEL_MONOCYPHER_DIR}/monocypher-ed25519.c")
  target_include_directories(cel_monocypher PUBLIC "${CEL_MONOCYPHER_DIR}")
  target_link_libraries(cellic PRIVATE cel_monocypher)
  if(CEL_DEV_LICENSE)
    target_compile_definitions(cellic PRIVATE CEL_DEV_LICENSE=1)
  endif()
  if(WIN32)
    target_link_libraries(cellic PRIVATE shell32 ole32)
  endif()
endif()
cel_require_target(cellic)
target_link_libraries(cellic PUBLIC cel_includes)
if(CEL_DEV_LICENSE)
  message(WARNING "Reafilm: CEL_DEV_LICENSE=ON - this build accepts the DEVELOPMENT license key. Never ship it.")
endif()

# ---- celgpu: GPU backends (src/gpu, owned by the GPU engineer; see src/gpu/README.md) --------------
add_subdirectory(src/gpu)
cel_require_target(celgpu)
target_link_libraries(celgpu PUBLIC cel_includes)
# our warning policy on every first-party library (components may add their own flags)
foreach(_t celcore celtex cellic celgpu)
  cel_set_warnings(${_t})
endforeach()

# ---- the OFX plug-in ---------------------------------------------------------------------------------
add_subdirectory(src/ofx)

# ---- tools and tests ---------------------------------------------------------------------------------
if(CEL_BUILD_TOOLS)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/licensegen/CMakeLists.txt")
    add_subdirectory(tools/licensegen)
  endif()
  # ofxhost: the Linux workbench host; on macOS / Windows it drives the CI smoke test of the bundle
  if((CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CEL_BUILD_TESTS) AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ofxhost/CMakeLists.txt")
    add_subdirectory(tools/ofxhost)
  endif()
endif()

if(CEL_BUILD_TESTS)
  file(GLOB _cel_test_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/tests/*")
  list(SORT _cel_test_dirs)
  foreach(_d IN LISTS _cel_test_dirs)
    if(IS_DIRECTORY "${_d}" AND EXISTS "${_d}/CMakeLists.txt")
      add_subdirectory("${_d}")
    endif()
  endforeach()
endif()

# ---- summary -------------------------------------------------------------------------------------------
message(STATUS "Reafilm Pro ${PROJECT_VERSION} (build ${CEL_BUILD_ID}) - ${CMAKE_SYSTEM_NAME} ${CMAKE_OSX_ARCHITECTURES}")
message(STATUS "  OpenFX:  ${CEL_OPENFX_DIR}")
message(STATUS "  GPU:     OpenCL=${CEL_ENABLE_OPENCL} CUDA=${CEL_ENABLE_CUDA} Metal=${CEL_ENABLE_METAL} ")
if(CEL_LICENSING)
  set(_cel_variant "licensed (keys, trial watermark)")
else()
  set(_cel_variant "personal (no licensing)")
endif()
message(STATUS "  Build:   ${_cel_variant}")
message(STATUS "  Tests:   ${CEL_BUILD_TESTS}  Dev license: ${CEL_DEV_LICENSE}")
message(STATUS "  Bundle:  ${CEL_BUNDLE_DIR}")
