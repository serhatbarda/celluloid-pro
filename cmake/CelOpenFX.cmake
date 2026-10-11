# Reafilm Pro - OpenFX SDK (headers + C++ Support library), pinned (SPEC §11).
#
# The sources come from FetchContent at the pinned commit. To build offline or against a local
# clone, pass -DFETCHCONTENT_SOURCE_DIR_OPENFX=<path> (e.g. external-openfx-ref/). Only the C
# headers (include/) and the Support library (Support/Library, Support/include) are used; the
# SDK's own CMake project (Conan based) is NOT added to the build.
#
# Defines: CEL_OPENFX_DIR and the static library target `cel_ofxsupport` (PUBLIC include dirs:
# OpenFX include/ and Support/include/, as SYSTEM so their warnings do not leak into our code).
include(FetchContent)

set(CEL_OPENFX_COMMIT "e40728885390ec16276d11e00025de9b4282060c")

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(openfx
  GIT_REPOSITORY https://github.com/AcademySoftwareFoundation/openfx.git
  GIT_TAG        ${CEL_OPENFX_COMMIT}
  # a sub directory without CMakeLists.txt: MakeAvailable only populates the sources
  SOURCE_SUBDIR  celluloid-populate-only)
FetchContent_MakeAvailable(openfx)
set(CEL_OPENFX_DIR "${openfx_SOURCE_DIR}")

foreach(_f include/ofxImageEffect.h include/ofxGPURender.h Support/include/ofxsImageEffect.h
           Support/Library/ofxsImageEffect.cpp)
  if(NOT EXISTS "${CEL_OPENFX_DIR}/${_f}")
    message(FATAL_ERROR "OpenFX sources at ${CEL_OPENFX_DIR} are incomplete (missing ${_f})")
  endif()
endforeach()

# Warn when a local clone is not at the pinned commit (the pin is what CI builds).
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CEL_OPENFX_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${CEL_OPENFX_DIR}"
                  OUTPUT_VARIABLE _cel_ofx_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_cel_ofx_head AND NOT _cel_ofx_head STREQUAL CEL_OPENFX_COMMIT)
    message(WARNING "OpenFX at ${CEL_OPENFX_DIR} is at ${_cel_ofx_head}, expected the pinned ${CEL_OPENFX_COMMIT}")
  endif()
endif()

file(GLOB CEL_OFXSUPPORT_SOURCES "${CEL_OPENFX_DIR}/Support/Library/*.cpp")
add_library(cel_ofxsupport STATIC ${CEL_OFXSUPPORT_SOURCES})
target_include_directories(cel_ofxsupport SYSTEM PUBLIC
  "${CEL_OPENFX_DIR}/include"
  "${CEL_OPENFX_DIR}/Support/include")
target_include_directories(cel_ofxsupport PRIVATE "${CEL_OPENFX_DIR}/Support/Library")
target_compile_features(cel_ofxsupport PUBLIC cxx_std_17)
# Third-party code: compiled without our warning flags.
if(MSVC)
  target_compile_options(cel_ofxsupport PRIVATE /W0 /utf-8)
  target_compile_definitions(cel_ofxsupport PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(cel_ofxsupport PRIVATE -w)
endif()
# The Support library writes a log file only when DEBUG is defined; make sure it never is.
target_compile_options(cel_ofxsupport PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/UDEBUG>" "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UDEBUG>")
