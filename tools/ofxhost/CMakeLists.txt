# ofxhost - headless OpenFX test host + SDK example plug-ins used to validate it.
#
# Standalone:   cmake -S tools/ofxhost -B /tmp/cel-host -G Ninja && cmake --build /tmp/cel-host
#               ctest --test-dir /tmp/cel-host --output-on-failure
# As a subdirectory of the main project the same targets are available
# (SDK example plug-ins only when OFXHOST_BUILD_SDK_PLUGINS=ON).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(ofxhost LANGUAGES C CXX)
  set(OFXHOST_STANDALONE ON)
  enable_testing()
else()
  set(OFXHOST_STANDALONE OFF)
endif()

# Linux: the full workbench host (OpenCL GPU path via POCL, SDK example plug-ins, render tests).
# macOS / Windows: the same host for the CI smoke test of the shipping bundle (macOS also drives the
# plug-in's Metal path with a host MTLCommandQueue + MTLBuffers).
set(_ofxhost_linux OFF)
if(UNIX AND NOT APPLE)
  set(_ofxhost_linux ON)
endif()
if(_ofxhost_linux)
  option(OFXHOST_BUILD_SDK_PLUGINS "Build OpenFX SDK example plug-ins and the host self-tests" ${OFXHOST_STANDALONE})
else()
  set(OFXHOST_BUILD_SDK_PLUGINS OFF)
endif()
option(OFXHOST_WITH_OPENCL "Enable the OpenCL GPU render path of the host" ${_ofxhost_linux})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- OpenFX SDK
# Resolution order: OFXHOST_OPENFX_DIR, an already populated 'openfx' FetchContent
# project (main build), FETCHCONTENT_SOURCE_DIR_OPENFX, the local clone
# <repo>/external-openfx-ref, else FetchContent of the pinned commit.
set(OFXHOST_OPENFX_DIR "" CACHE PATH "OpenFX SDK root (contains include/ofxCore.h)")
set(_ofx_local "${CMAKE_CURRENT_SOURCE_DIR}/../../external-openfx-ref")
if(NOT OFXHOST_OPENFX_DIR)
  if(DEFINED openfx_SOURCE_DIR AND EXISTS "${openfx_SOURCE_DIR}/include/ofxCore.h")
    set(OFXHOST_OPENFX_DIR "${openfx_SOURCE_DIR}")
  elseif(FETCHCONTENT_SOURCE_DIR_OPENFX AND EXISTS "${FETCHCONTENT_SOURCE_DIR_OPENFX}/include/ofxCore.h")
    set(OFXHOST_OPENFX_DIR "${FETCHCONTENT_SOURCE_DIR_OPENFX}")
  elseif(EXISTS "${_ofx_local}/include/ofxCore.h")
    get_filename_component(OFXHOST_OPENFX_DIR "${_ofx_local}" ABSOLUTE)
  else()
    include(FetchContent)
    FetchContent_Declare(openfx
      GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
      GIT_TAG e40728885390ec16276d11e00025de9b4282060c
      SOURCE_SUBDIR do-not-build)
    FetchContent_MakeAvailable(openfx)
    set(OFXHOST_OPENFX_DIR "${openfx_SOURCE_DIR}")
  endif()
endif()
message(STATUS "ofxhost: OpenFX SDK at ${OFXHOST_OPENFX_DIR}")
set(OFXHOST_OFX_INCLUDE "${OFXHOST_OPENFX_DIR}/include")

find_package(Threads REQUIRED)
set(OFXHOST_HAVE_OPENCL OFF)
if(OFXHOST_WITH_OPENCL)
  find_package(OpenCL QUIET)
  if(OpenCL_FOUND)
    set(OFXHOST_HAVE_OPENCL ON)
  else()
    message(WARNING "ofxhost: OpenCL not found - the --gpu opencl path is disabled")
  endif()
endif()

# ---------------------------------------------------------------- host
add_executable(ofxhost
  src/main.cpp
  src/Host.cpp
  src/Instance.cpp
  src/Props.cpp
  src/Params.cpp
  src/Image.cpp
  src/Png.cpp
  src/OpenCLHost.cpp)
target_include_directories(ofxhost PRIVATE "${OFXHOST_OFX_INCLUDE}" src)
target_link_libraries(ofxhost PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(MSVC)
  target_compile_options(ofxhost PRIVATE /W3 /utf-8 /EHsc)
  target_compile_definitions(ofxhost PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(ofxhost PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()
if(APPLE)
  enable_language(OBJCXX)
  target_sources(ofxhost PRIVATE src/MetalHost.mm)
  set_source_files_properties(src/MetalHost.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
  target_compile_definitions(ofxhost PRIVATE OFXHOST_HAVE_METAL)
  target_link_libraries(ofxhost PRIVATE "-framework Metal" "-framework Foundation")
else()
  target_sources(ofxhost PRIVATE src/MetalHostStub.cpp)
endif()
if(OFXHOST_HAVE_OPENCL)
  target_compile_definitions(ofxhost PRIVATE OFXHOST_HAVE_OPENCL CL_TARGET_OPENCL_VERSION=120)
  target_link_libraries(ofxhost PRIVATE OpenCL::OpenCL)
endif()

# ---------------------------------------------------------------- SDK example plug-ins (self-test fixtures)
if(OFXHOST_BUILD_SDK_PLUGINS)
  set(_sdk "${OFXHOST_OPENFX_DIR}")
  set(OFXHOST_SDK_BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/sdk-plugins")
  file(GLOB _support_src "${_sdk}/Support/Library/*.cpp")
  add_library(ofxhost_ofxsupport STATIC ${_support_src})
  set_target_properties(ofxhost_ofxsupport PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
  target_include_directories(ofxhost_ofxsupport PUBLIC "${_sdk}/include" "${_sdk}/Support/include" "${_sdk}/Support/Library")
  target_compile_options(ofxhost_ofxsupport PRIVATE -w)
  if(OFXHOST_HAVE_OPENCL)
    target_compile_definitions(ofxhost_ofxsupport PUBLIC OFX_SUPPORTS_OPENCLRENDER CL_TARGET_OPENCL_VERSION=120)
  endif()

  function(ofxhost_add_bundle name)
    cmake_parse_arguments(B "SUPPORT;OPENCL;GL" "" "SOURCES" ${ARGN})
    add_library(${name} MODULE ${B_SOURCES})
    set_target_properties(${name} PROPERTIES PREFIX "" SUFFIX ".ofx" CXX_VISIBILITY_PRESET hidden
      LIBRARY_OUTPUT_DIRECTORY "${OFXHOST_SDK_BUNDLE_DIR}/${name}.ofx.bundle/Contents/Linux-x86-64")
    target_include_directories(${name} PRIVATE "${_sdk}/include" "${_sdk}/Examples/include" "${_sdk}/Support/Plugins/include")
    target_compile_options(${name} PRIVATE -w)
    if(B_SUPPORT)
      target_link_libraries(${name} PRIVATE ofxhost_ofxsupport)
    endif()
    if(B_GL)
      target_link_libraries(${name} PRIVATE OpenGL::GL)
    endif()
    if(B_OPENCL)
      target_compile_definitions(${name} PRIVATE OFX_SUPPORTS_OPENCLRENDER CL_TARGET_OPENCL_VERSION=120)
      target_link_libraries(${name} PRIVATE OpenCL::OpenCL)
    endif()
    # the C API examples export their entry points with default visibility
    target_compile_options(${name} PRIVATE -fvisibility=default)
  endfunction()

  # C API examples
  ofxhost_add_bundle(sdk_invert SOURCES "${_sdk}/Examples/Invert/invert.cpp")
  ofxhost_add_bundle(sdk_basic SOURCES "${_sdk}/Examples/Basic/basic.cpp")
  # OFX C++ Support library examples
  ofxhost_add_bundle(sdks_invert SUPPORT SOURCES "${_sdk}/Support/Plugins/Invert/invert.cpp")
  # these three draw overlay interacts with legacy OpenGL (never invoked: the host has no viewer)
  find_package(OpenGL QUIET)
  if(OpenGL_FOUND)
    ofxhost_add_bundle(sdks_basic SUPPORT GL SOURCES "${_sdk}/Support/Plugins/Basic/basic.cpp")
    ofxhost_add_bundle(sdks_choiceparams SUPPORT GL SOURCES "${_sdk}/Support/Plugins/ChoiceParams/choiceparams.cpp")
    ofxhost_add_bundle(sdks_tester SUPPORT GL SOURCES "${_sdk}/Support/Plugins/Tester/Tester.cpp")
  endif()
  if(OFXHOST_HAVE_OPENCL)
    ofxhost_add_bundle(sdks_gpugain SUPPORT OPENCL SOURCES
      "${_sdk}/Support/Plugins/GPUGain/GPUGain.cpp" "${_sdk}/Support/Plugins/GPUGain/OpenCLKernel.cpp")
  endif()

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME ofxhost_sdk_selftest
      COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sdk_plugins.py"
              --host $<TARGET_FILE:ofxhost> --bundles "${OFXHOST_SDK_BUNDLE_DIR}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/selftest" --opencl $<BOOL:${OFXHOST_HAVE_OPENCL}>)
  endif()
endif()

# ---------------------------------------------------------------- Reafilm Pro plug-in: end-to-end smoke test
# Loads the built bundle through the real OFX entry points on every platform (CI: macOS universal
# bundle, Windows Win64 DLL): describe + GPU flags vs self test, CPU render vs the committed
# reference, trial LUT refusal; ofx_smoke_gpu renders through the plug-in's Metal (macOS) or OpenCL
# (Linux/POCL) path with host-created queues and buffers and compares with the CPU (2e-4).
if(DEFINED CEL_BUNDLE_DIR AND TARGET reafilm_ofx)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    set(_smoke "${CMAKE_CURRENT_SOURCE_DIR}/smoke/ofx_smoke.py")
    add_test(NAME ofx_smoke
      COMMAND ${Python3_EXECUTABLE} "${_smoke}" --host $<TARGET_FILE:ofxhost> --bundle "${CEL_BUNDLE_DIR}"
              --work "${CMAKE_CURRENT_BINARY_DIR}/smoke_cpu")
    set_tests_properties(ofx_smoke PROPERTIES LABELS "ofx" TIMEOUT 600)
    set(_smoke_gpu "")
    if(APPLE)
      set(_smoke_gpu metal)
    elseif(OFXHOST_HAVE_OPENCL)
      set(_smoke_gpu opencl)
    endif()
    if(_smoke_gpu)
      add_test(NAME ofx_smoke_gpu
        COMMAND ${Python3_EXECUTABLE} "${_smoke}" --host $<TARGET_FILE:ofxhost> --bundle "${CEL_BUNDLE_DIR}"
                --work "${CMAKE_CURRENT_BINARY_DIR}/smoke_gpu" --gpu ${_smoke_gpu})
      set_tests_properties(ofx_smoke_gpu PROPERTIES LABELS "ofx;gpu-device" TIMEOUT 900)
    endif()
    # the manual's controls reference is generated from the plug-in's own parameter description:
    # fails when a label, range, default or hint changed without regenerating docs/MANUAL.md.
    # The manual documents the default (personal) build; the licensed build shows the License group
    # and licensed-only hints, so the check is registered for the personal build only.
    if(NOT CEL_LICENSING)
      add_test(NAME docs_manual_controls
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../docs/gen_manual_controls.py" --check
                --ofxhost $<TARGET_FILE:ofxhost> --bundle "${CEL_BUNDLE_DIR}")
      set_tests_properties(docs_manual_controls PROPERTIES LABELS "ofx;docs" TIMEOUT 300)
    endif()
  endif()
endif()

# ---------------------------------------------------------------- Reafilm Pro plug-in: GPU self test at describe
if(_ofxhost_linux AND DEFINED CEL_BUNDLE_DIR AND TARGET reafilm_ofx)
  add_test(NAME ofx_gpu_selftest_logged
    COMMAND "${CMAKE_COMMAND}" -DHOST=$<TARGET_FILE:ofxhost> "-DBUNDLE=${CEL_BUNDLE_DIR}"
            "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/selftest_params.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CelCheckSelfTestLog.cmake")
  set_tests_properties(ofx_gpu_selftest_logged PROPERTIES LABELS "ofx;linux-only" TIMEOUT 300)
endif()

# ---------------------------------------------------------------- Reafilm Pro render tests (opt-in)
# ctest -L render runs tools/render_tests/run_all.sh --quick against the bundle of the main build
# (or OFXHOST_RENDER_TEST_BUNDLE). Opt-in because it renders hundreds of frames and its §6 look
# checks are acceptance criteria, not unit tests.
option(OFXHOST_RENDER_TESTS "Register the Reafilm Pro render tests with CTest (label 'render')" OFF)
set(OFXHOST_RENDER_TEST_BUNDLE "" CACHE PATH "ReafilmPro.ofx.bundle for the render tests (default: the main build's bundle)")
if(OFXHOST_RENDER_TESTS)
  set(_rt_bundle "${OFXHOST_RENDER_TEST_BUNDLE}")
  if(NOT _rt_bundle AND DEFINED CEL_BUNDLE_DIR)
    set(_rt_bundle "${CEL_BUNDLE_DIR}")
  endif()
  if(NOT _rt_bundle)
    message(FATAL_ERROR "OFXHOST_RENDER_TESTS=ON needs OFXHOST_RENDER_TEST_BUNDLE (or the main project's CEL_BUNDLE_DIR)")
  endif()
  set(_rt_args --quick)
  if(CEL_DEV_LICENSE)
    list(APPEND _rt_args --dev-license)
  endif()
  add_test(NAME reafilm_render_tests
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../render_tests/run_all.sh" "${_rt_bundle}" "${CMAKE_BINARY_DIR}/render-tests" ${_rt_args})
  set_tests_properties(reafilm_render_tests PROPERTIES LABELS render TIMEOUT 7200
    ENVIRONMENT "OFXHOST=$<TARGET_FILE:ofxhost>")
endif()
