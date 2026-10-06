# tests/texture - unit tests + preview renders for the texture passes, plan builder and CPU
# backend (SPEC §3, §5).  Picked up by the top-level CMakeLists.txt (tests/* with a CMakeLists.txt)
# after the celtex / celcore targets exist.  Stand-alone build without CMake: ./build.sh
#
#   cel_texture_tests  [--quick] [--no-timing]   registered with CTest as "texture" (--no-timing)
#   cel_tex_preview    <outdir> [grain|glow|acut|weave|all]   PNG contact sheets for visual review
if(NOT TARGET celtex)
  message(WARNING "tests/texture: target celtex not defined, texture tests skipped")
  return()
endif()

add_executable(cel_texture_tests test_texture.cpp)
target_link_libraries(cel_texture_tests PRIVATE celtex celcore Threads::Threads)
target_include_directories(cel_texture_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(COMMAND cel_set_warnings)
  cel_set_warnings(cel_texture_tests)
endif()
add_test(NAME texture COMMAND cel_texture_tests --no-timing)
set_tests_properties(texture PROPERTIES TIMEOUT 900)

add_executable(cel_tex_preview tex_preview.cpp)
target_link_libraries(cel_tex_preview PRIVATE celtex celcore Threads::Threads)
target_include_directories(cel_tex_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(COMMAND cel_set_warnings)
  cel_set_warnings(cel_tex_preview)
endif()
