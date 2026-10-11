# ctest helper: run ofxhost on the plug-in with REAFILM_LOG=1 and require the describe-time GPU
# self test in the log (regression guard: CEL_HAVE_GPU_API must reach the plug-in build, otherwise
# compiled-in GPU backends are declared without being tested).
#   cmake -DHOST=<ofxhost> -DBUNDLE=<ReafilmPro.ofx.bundle> -DOUT=<json> -P CelCheckSelfTestLog.cmake
set(ENV{REAFILM_LOG} 1)
execute_process(COMMAND "${HOST}" "${BUNDLE}" --list-params-json "${OUT}"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "ofxhost failed (${rc}):\n${out}\n${err}")
endif()
string(REGEX MATCHALL "[A-Za-z]+ self test: [^\n]*" lines "${err}")
if(NOT lines)
  message(FATAL_ERROR "no 'self test:' line in the plug-in log - the GPU self test is not compiled in:\n${err}")
endif()
foreach(l IN LISTS lines)
  message(STATUS "${l}")
endforeach()
string(REGEX MATCH "GPU support declared: [^\n]*" decl "${err}")
message(STATUS "${decl}")
