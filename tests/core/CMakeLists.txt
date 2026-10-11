# Reafilm Pro - core unit tests. Use via add_subdirectory(tests/core) (needs target celcore,
# which is created here from src/core if the parent project has not defined it).
cmake_minimum_required(VERSION 3.20)
get_filename_component(CEL_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT TARGET celcore)
  add_subdirectory(${CEL_ROOT_DIR}/src/core ${CMAKE_CURRENT_BINARY_DIR}/celcore)
endif()
add_executable(test_core test_core.cpp)
target_link_libraries(test_core PRIVATE celcore)
target_include_directories(test_core PRIVATE ${CEL_ROOT_DIR}/src)
target_compile_definitions(test_core PRIVATE CEL_REFERENCE_DIR="${CEL_ROOT_DIR}/reference")
if(NOT MSVC)
  target_compile_options(test_core PRIVATE -Wall -Wextra)
endif()
if(COMMAND add_test)
  add_test(NAME core COMMAND test_core --tmp ${CMAKE_CURRENT_BINARY_DIR})
endif()

# Look-design gates on the NumPy reference model (Linux workbench; needs python3 + numpy):
# distinctness of every negative / print / Lab Scan rendering (min mean dE2000 >= 2) and the
# hue-stability aims of reference/aims.py on the gated stock pairs.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(COMMAND add_test AND Python3_Interpreter_FOUND AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME look_gates_distinct COMMAND ${Python3_EXECUTABLE} ${CEL_ROOT_DIR}/reference/design.py gates)
  add_test(NAME look_gates_aims COMMAND ${Python3_EXECUTABLE} ${CEL_ROOT_DIR}/reference/aims.py gate)
  set_tests_properties(look_gates_distinct look_gates_aims PROPERTIES LABELS "linux-only")
endif()
