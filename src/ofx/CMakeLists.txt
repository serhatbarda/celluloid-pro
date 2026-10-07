# Celluloid Pro - the OFX plug-in module and its bundle (SPEC §7, §11).
#
#   ${CEL_BUNDLE_DIR} = <build>/Celluloid.ofx.bundle
#     Contents/Info.plist
#     Contents/<MacOS | Win64 | Linux-x86-64>/Celluloid.ofx
#     Contents/Resources/de.serhatbarda.CelluloidPro.png     (host icon, OFX convention)
#     Contents/Resources/MANUAL.md, Licenses/*                 (docs, third-party notices)

# ---- bundle architecture directory (OFX spec "Packaging OFX Plug-ins") ----------------------
if(APPLE)
  set(CEL_OFX_ARCH "MacOS")
elseif(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CEL_OFX_ARCH "Win64")
  else()
    set(CEL_OFX_ARCH "Win32")
  endif()
else()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(CEL_OFX_ARCH "Linux-x86-64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CEL_OFX_ARCH "Linux-aarch64")
  else()
    set(CEL_OFX_ARCH "Linux-x86")
  endif()
endif()
set(CEL_OFX_ARCH "${CEL_OFX_ARCH}" PARENT_SCOPE)
set(CEL_BUNDLE_BINARY_DIR "${CEL_BUNDLE_DIR}/Contents/${CEL_OFX_ARCH}")

configure_file(Version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/CelVersion.h" @ONLY)
# build id / date regenerated on every build (the configure-time value goes stale)
add_custom_target(cel_build_id ALL
  COMMAND "${CMAKE_COMMAND}" "-DSRC=${PROJECT_SOURCE_DIR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/generated/CelBuildId.h"
          "-DFORCED=$CACHE{CEL_BUILD_ID}" -P "${PROJECT_SOURCE_DIR}/cmake/CelBuildIdScript.cmake"
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/CelBuildId.h"
  COMMENT "Celluloid build id"
  VERBATIM)

set(CEL_OFX_SOURCES
  CelluloidPlugin.cpp
  Params.cpp
  Platform.cpp
  Presets.cpp)

add_library(celluloid_ofx MODULE ${CEL_OFX_SOURCES})
target_include_directories(celluloid_ofx PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(celluloid_ofx PRIVATE cel_ofxsupport celgpu celtex celcore cellic)
add_dependencies(celluloid_ofx cel_build_id)
if(CEL_HAVE_GPU_API)
  target_compile_definitions(celluloid_ofx PRIVATE CEL_HAVE_GPU_API=1)
endif()
cel_set_warnings(celluloid_ofx)

set_target_properties(celluloid_ofx PROPERTIES
  OUTPUT_NAME "Celluloid"
  PREFIX ""
  SUFFIX ".ofx"
  LIBRARY_OUTPUT_DIRECTORY "${CEL_BUNDLE_BINARY_DIR}"
  # MSVC writes an import library/.exp for the exported entry points: keep them out of the bundle
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")
foreach(_cfg IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${_cfg}" _CFG)
  set_target_properties(celluloid_ofx PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY_${_CFG} "${CEL_BUNDLE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/lib"
    PDB_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_BINARY_DIR}/pdb")
endforeach()

# ---- exported symbols: only OfxGetNumberOfPlugins / OfxGetPlugin --------------------------------
if(APPLE)
  target_link_options(celluloid_ofx PRIVATE
    "LINKER:-exported_symbols_list,${PROJECT_SOURCE_DIR}/cmake/exports/ofx-macos.exp"
    "LINKER:-dead_strip")
  set_property(TARGET celluloid_ofx APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/exports/ofx-macos.exp")
elseif(WIN32)
  # OfxExport (__declspec(dllexport)) in ofxCore.h exports exactly the two entry points.
  target_link_libraries(celluloid_ofx PRIVATE shell32 ole32)
  if(MSVC)
    target_link_options(celluloid_ofx PRIVATE /OPT:REF /OPT:ICF)
  endif()
else()
  target_link_options(celluloid_ofx PRIVATE
    "LINKER:--version-script=${PROJECT_SOURCE_DIR}/cmake/exports/ofx-linux.map"
    "LINKER:-z,defs"              # no unresolved symbols in the module
    "LINKER:--gc-sections"
    "LINKER:-z,noexecstack")
  set_property(TARGET celluloid_ofx APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/exports/ofx-linux.map")
  target_link_libraries(celluloid_ofx PRIVATE ${CMAKE_DL_LIBS})
endif()

# ---- bundle resources ------------------------------------------------------------------------------
configure_file("${PROJECT_SOURCE_DIR}/resources/Info.plist.in" "${CEL_BUNDLE_DIR}/Contents/Info.plist" @ONLY)

set(CEL_BUNDLE_RES "${CEL_BUNDLE_DIR}/Contents/Resources")
set(_cel_res_cmds "")
macro(cel_bundle_copy src dst)
  if(EXISTS "${src}")
    list(APPEND _cel_res_cmds COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dst}")
  endif()
endmacro()
cel_bundle_copy("${PROJECT_SOURCE_DIR}/resources/${CEL_PLUGIN_IDENTIFIER}.png" "${CEL_BUNDLE_RES}/${CEL_PLUGIN_IDENTIFIER}.png")
cel_bundle_copy("${PROJECT_SOURCE_DIR}/docs/MANUAL.md" "${CEL_BUNDLE_RES}/MANUAL.md")
cel_bundle_copy("${CEL_OPENFX_DIR}/Support/LICENSE" "${CEL_BUNDLE_RES}/Licenses/OpenFX-Support-LICENSE.txt")
cel_bundle_copy("${CEL_OPENFX_DIR}/LICENSE.md" "${CEL_BUNDLE_RES}/Licenses/OpenFX-LICENSE.txt")
cel_bundle_copy("${PROJECT_SOURCE_DIR}/third_party/monocypher/LICENCE.md" "${CEL_BUNDLE_RES}/Licenses/Monocypher-LICENCE.txt")
cel_bundle_copy("${PROJECT_SOURCE_DIR}/third_party/opencl/LICENSE" "${CEL_BUNDLE_RES}/Licenses/Khronos-OpenCL-Headers-LICENSE.txt")
cel_bundle_copy("${PROJECT_SOURCE_DIR}/resources/THIRD_PARTY_NOTICES.txt" "${CEL_BUNDLE_RES}/Licenses/THIRD_PARTY_NOTICES.txt")
add_custom_command(TARGET celluloid_ofx POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CEL_BUNDLE_RES}/Licenses"
  ${_cel_res_cmds}
  COMMENT "Assembling ${CEL_BUNDLE_NAME} resources"
  VERBATIM)

# ---- install: the whole bundle (installers package the build-tree bundle directly) --------------
install(DIRECTORY "${CEL_BUNDLE_DIR}" DESTINATION "." USE_SOURCE_PERMISSIONS COMPONENT plugin)

# ---- self test of the parameter registry and the preset table ------------------------------------
if(CEL_BUILD_TESTS)
  add_executable(cel_ofx_selftest SelfTest.cpp Params.cpp Presets.cpp Platform.cpp)
  target_include_directories(cel_ofx_selftest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(cel_ofx_selftest PRIVATE celcore)
  if(WIN32)
    target_link_libraries(cel_ofx_selftest PRIVATE shell32 ole32)
  endif()
  cel_set_warnings(cel_ofx_selftest)
  add_test(NAME ofx_selftest COMMAND cel_ofx_selftest)
  set_tests_properties(ofx_selftest PROPERTIES LABELS "ofx")
endif()
