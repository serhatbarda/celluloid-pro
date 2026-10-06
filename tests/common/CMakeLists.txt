# tests/common - parallelRows() worker pool (src/common/Common.h) and the CPU branch of the
# portable math in src/kernels/Portable.h. Registered with CTest as "common".
add_executable(test_common test_common.cpp)
target_link_libraries(test_common PRIVATE cel_includes Threads::Threads)
if(COMMAND cel_set_warnings)
  cel_set_warnings(test_common)
endif()
add_test(NAME common COMMAND test_common)
