# Reafilm Pro - licensing (SPEC §9).
#   cel_monocypher  vendored Monocypher 4.0.3 (Ed25519, RFC 8032), C99
#   cellic          verifyKey / saveKey / removeKey / current  (linked into the plugin of the licensed
#                   build, -DCEL_LICENSING=ON; built and tested in every configuration)
#   cellic_sign     key issuing (tools/licensegen and tests only, never the plugin)
# Usage from the top-level CMakeLists.txt:  add_subdirectory(src/license)
if(NOT CMAKE_C_COMPILER_LOADED)
  enable_language(C)
endif()

option(CEL_DEV_LICENSE "Accept the DEVELOPMENT license key from tools/licensegen/dev-keys (never for releases)" OFF)

set(CEL_LICENSE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE INTERNAL "")
get_filename_component(CEL_LICENSE_ROOT "${CEL_LICENSE_ROOT}" ABSOLUTE)

add_library(cel_monocypher STATIC
  ${CEL_LICENSE_ROOT}/third_party/monocypher/monocypher.c
  ${CEL_LICENSE_ROOT}/third_party/monocypher/monocypher-ed25519.c)
set_target_properties(cel_monocypher PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
if(MSVC)
  # third-party code: keep the warning level modest; it is tested upstream on MSVC
  target_compile_options(cel_monocypher PRIVATE /W3)
endif()

add_library(cellic STATIC
  License.cpp
  Persist.cpp
  KeyFormat.cpp)
target_include_directories(cellic PUBLIC ${CEL_LICENSE_ROOT}/src)
target_compile_features(cellic PUBLIC cxx_std_17)
target_link_libraries(cellic PRIVATE cel_monocypher)
set_target_properties(cellic PROPERTIES
  POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
if(WIN32)
  target_link_libraries(cellic PRIVATE shell32 ole32)
endif()
if(CEL_DEV_LICENSE)
  target_compile_definitions(cellic PRIVATE CEL_DEV_LICENSE=1)
  message(STATUS "cellic: CEL_DEV_LICENSE=ON - accepts the committed DEVELOPMENT license key (never ship this build)")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/PublicKey.h" _cel_pubkey_h)
if(_cel_pubkey_h MATCHES "kProductionKeyProvisional = true")
  message(STATUS "Reafilm licensing: PROVISIONAL production key in src/license/PublicKey.h "
                 "(regenerate with tools/licensegen before the first commercial release)")
endif()

add_library(cellic_sign STATIC KeySigner.cpp)
target_link_libraries(cellic_sign PUBLIC cellic PRIVATE cel_monocypher)
if(WIN32)
  target_link_libraries(cellic_sign PRIVATE bcrypt)
endif()
