# tests/license - unit tests for licensing and the watermark kernel (SPEC §9).
#
# Fragment for the top-level build (after add_subdirectory(src/license)):
#     if(CEL_BUILD_TESTS) add_subdirectory(tests/license) endif()
# It can also be configured on its own:  cmake -S tests/license -B <dir>
#
# The tests compile the license sources directly in two variants, independent of how `cellic`
# is configured:  cel_license_tests (development key accepted -> persistence tests run) and
# cel_license_tests_release (no development key -> checks that it is rejected).
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(celluloid_license_tests C CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  enable_testing()
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../src/license ${CMAKE_CURRENT_BINARY_DIR}/cellic)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../tools/licensegen ${CMAKE_CURRENT_BINARY_DIR}/licensegen)
endif()

get_filename_component(_cel_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(_cel_lic_src
  ${_cel_root}/src/license/License.cpp
  ${_cel_root}/src/license/Persist.cpp
  ${_cel_root}/src/license/KeyFormat.cpp
  ${_cel_root}/src/license/KeySigner.cpp)
set(_cel_test_src
  ${CMAKE_CURRENT_SOURCE_DIR}/TestMain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test_keys.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test_persist.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test_watermark.cpp)

foreach(_variant dev release)
  if(_variant STREQUAL "dev")
    set(_t cel_license_tests)
  else()
    set(_t cel_license_tests_release)
  endif()
  add_executable(${_t} ${_cel_test_src} ${_cel_lic_src})
  target_link_libraries(${_t} PRIVATE cel_monocypher)
  target_compile_features(${_t} PRIVATE cxx_std_17)
  target_compile_definitions(${_t} PRIVATE CEL_LICENSE_REPO_ROOT="${_cel_root}")
  if(_variant STREQUAL "dev")
    target_compile_definitions(${_t} PRIVATE CEL_DEV_LICENSE=1)
  endif()
  if(WIN32)
    target_link_libraries(${_t} PRIVATE shell32 ole32 bcrypt)
  endif()
  find_package(Threads)
  if(Threads_FOUND)
    target_link_libraries(${_t} PRIVATE Threads::Threads)
  endif()
  if(_variant STREQUAL "dev")
    add_test(NAME ${_t} COMMAND ${_t} --out ${CMAKE_CURRENT_BINARY_DIR}/images)
  else()
    add_test(NAME ${_t} COMMAND ${_t})
  endif()
endforeach()
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/images)

# Optional: the watermark kernel compiled as OpenCL C and compared with the CPU build.
# (Linux dev box only, e.g. with POCL; macOS/Windows CI do not run it.)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(OpenCL QUIET)
endif()
if(OpenCL_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(cel_license_opencl_check ${CMAKE_CURRENT_SOURCE_DIR}/opencl_watermark_check.cpp)
  target_link_libraries(cel_license_opencl_check PRIVATE OpenCL::OpenCL)
  target_compile_features(cel_license_opencl_check PRIVATE cxx_std_17)
  add_test(NAME cel_license_opencl_check COMMAND cel_license_opencl_check ${_cel_root}/src/kernels)
  set_tests_properties(cel_license_opencl_check PROPERTIES SKIP_RETURN_CODE 77)
endif()
