# Reafilm Pro - offline OpenCL C 1.2 syntax check of flattened sources with clang (no device needed).
# (__OPENCL_VERSION__ is defined by every OpenCL runtime compiler; clang alone needs it explicitly)
# cmake -DCLANG=<clang> -DFILES=<a.cl;b.cl> -P check_opencl_syntax.cmake
set(rc_all 0)
foreach(f IN LISTS FILES)
  execute_process(COMMAND "${CLANG}" -x cl -cl-std=CL1.2 -D__OPENCL_VERSION__=120 -fsyntax-only -Xclang -finclude-default-header
                          -Werror -Wall -Wextra -Wdouble-promotion -Wno-unused-function -Wno-unused-parameter "${f}"
                  RESULT_VARIABLE rc ERROR_VARIABLE er)
  if(rc EQUAL 0)
    message(STATUS "OpenCL C syntax OK: ${f}")
  else()
    message(SEND_ERROR "OpenCL C syntax FAILED: ${f}\n${er}")
    set(rc_all 1)
  endif()
endforeach()
if(rc_all)
  message(FATAL_ERROR "OpenCL syntax check failed")
endif()
