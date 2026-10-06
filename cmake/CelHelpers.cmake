# Celluloid Pro - small CMake helpers.

# cel_set_warnings(<target>)
#   -Wall -Wextra on GCC/Clang, /W4 on MSVC; -Werror / /WX when
#   CEL_WARNINGS_AS_ERRORS is ON. Applies to C and C++ sources only (never to CUDA).
#   Unused-parameter warnings are disabled: every kernel has the uniform ABI signature
#   (CEL_KERNEL_PARAMS, KernelABI.h) and most kernels legitimately ignore some of its inputs.
function(cel_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/wd4100>")
    if(CEL_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/WX>)
    endif()
  else()
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:-Wall;-Wextra;-Wno-unused-parameter>")
    if(CEL_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:-Werror>)
    endif()
  endif()
endfunction()

# cel_collect_sources(<out-var> COMPONENT <name> DIRS <dir>...)
#   Collects *.cpp / *.c (and *.mm on Apple) from the given directories. When none exist and
#   CEL_STUBS_DIR is set, falls back to ${CEL_STUBS_DIR}/<name>/ (development builds while a
#   component is still being written). Fails if there is nothing to build.
function(cel_collect_sources out)
  cmake_parse_arguments(A "" "COMPONENT" "DIRS" ${ARGN})
  set(globs "")
  foreach(d IN LISTS A_DIRS)
    list(APPEND globs "${d}/*.cpp" "${d}/*.c")
    if(APPLE)
      list(APPEND globs "${d}/*.mm")
    endif()
  endforeach()
  set(srcs "")
  if(globs)
    file(GLOB srcs CONFIGURE_DEPENDS ${globs})
  endif()
  if(NOT srcs AND CEL_STUBS_DIR AND IS_DIRECTORY "${CEL_STUBS_DIR}/${A_COMPONENT}")
    file(GLOB srcs CONFIGURE_DEPENDS "${CEL_STUBS_DIR}/${A_COMPONENT}/*.cpp")
    if(srcs)
      message(WARNING "Celluloid: component '${A_COMPONENT}' uses DEVELOPMENT STUBS from ${CEL_STUBS_DIR}/${A_COMPONENT}")
    endif()
  endif()
  if(NOT srcs)
    message(FATAL_ERROR "Celluloid: no sources for component '${A_COMPONENT}' (looked in ${A_DIRS})")
  endif()
  list(SORT srcs)
  set(${out} ${srcs} PARENT_SCOPE)
endfunction()

# cel_require_target(<target>): fail clearly when a component's CMakeLists.txt did not define it.
function(cel_require_target t)
  if(NOT TARGET ${t})
    message(FATAL_ERROR "Celluloid: the component build did not define the target '${t}' (see SPEC §11)")
  endif()
endfunction()
