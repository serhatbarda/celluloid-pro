# Reafilm Pro - GPU backend tests (owned by the gpu role).
#   From the top level (after add_subdirectory(src/gpu)):   add_subdirectory(tests/gpu)
#   Standalone:                                              tests/gpu/build.sh [build-dir]
# Tests (ctest; labels: gpu-device = needs a GPU/POCL device, linux-only = Linux workbench tools):
#   gpu_flatten        cmake/FlattenKernels.cmake fixture test
#   gpu_abi            backends vs the reference executor on the ABI test kernel set (+ production
#                      self tests: builds the production kernels and runs the probe)
#   gpu_opencl_syntax  clang OpenCL C syntax check of the flattened sources (when clang exists; linux-only)
#   gpu_metal_shim     clang++ + address-space shim check of the flattened MSL (non-Apple, clang)
#   gpu_metal_objc_mock clang++ -fobjc-arc syntax check of the .mm files against mock headers (non-Apple)
#   gpu_metal_xcrun    `xcrun metal` compile of the flattened MSL (Apple only)
#   gpu_parity         production plans: CPU backend vs reference vs every GPU device (needs core+engine)
cmake_minimum_required(VERSION 3.20)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(reafilm_gpu_tests LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  enable_testing()
  if(CEL_ENABLE_CUDA)
    enable_language(CUDA)   # languages must be enabled at the top level to be usable everywhere
  endif()
  if(APPLE)
    enable_language(OBJCXX)
  endif()
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../src/gpu" "${CMAKE_BINARY_DIR}/celgpu")
endif()
if(NOT TARGET celgpu)
  message(FATAL_ERROR "tests/gpu needs the celgpu target (add_subdirectory(src/gpu) first)")
endif()

get_filename_component(CEL_GPU_TEST_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${CEL_GPU_TEST_REPO}/cmake/FlattenKernels.cmake")
set(_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_gen}")

add_test(NAME gpu_flatten
  COMMAND "${CMAKE_COMMAND}" -DREPO=${CEL_GPU_TEST_REPO} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/flatten
          -P "${CMAKE_CURRENT_SOURCE_DIR}/flatten/run_flatten_test.cmake")

# ABI test kernel set, flattened for OpenCL and Metal
cel_embed_kernel_source(
  ROOT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/abi/AbiOpenCL.cl" SYMBOL cel_opencl_abi
  OUTPUT_CPP "${_gen}/AbiOpenCLSource.cpp" OUTPUT_FLAT "${_gen}/reafilm_abi.cl"
  BASE_DIR "${CEL_GPU_TEST_REPO}"
  DEPENDS_DIRS "${CEL_GPU_TEST_REPO}/src/kernels" "${CEL_GPU_TEST_REPO}/src/gpu/kernels")
cel_embed_kernel_source(
  ROOT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/abi/AbiMetal.metal" SYMBOL cel_metal_abi
  OUTPUT_CPP "${_gen}/AbiMetalSource.cpp" OUTPUT_FLAT "${_gen}/reafilm_abi.metal"
  BASE_DIR "${CEL_GPU_TEST_REPO}"
  DEPENDS_DIRS "${CEL_GPU_TEST_REPO}/src/kernels" "${CEL_GPU_TEST_REPO}/src/gpu/kernels")
add_custom_target(celgpu_test_kernel_sources DEPENDS "${_gen}/reafilm_abi.cl" "${_gen}/reafilm_abi.metal")

add_executable(test_gpu_abi test_gpu_abi.cpp test_env_opencl.cpp test_env_cuda.cpp
  GpuTestEnv.h RefBackend.h abi/AbiKernels.h abi/AbiKernelList.h)
target_link_libraries(test_gpu_abi PRIVATE celgpu)
target_include_directories(test_gpu_abi PRIVATE ${CEL_GPU_KERNEL_INCLUDE_DIRS})
find_package(Threads REQUIRED)
target_link_libraries(test_gpu_abi PRIVATE Threads::Threads)
# Host-compiler warning flags are gated by language: nvcc would read a bare /W4 as an input file.
# C4100 / -Wunused-parameter: every kernel has the uniform ABI signature (see cel_set_warnings).
if(MSVC)
  target_compile_options(test_gpu_abi PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/wd4100>")
else()
  target_compile_options(test_gpu_abi PRIVATE "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall;-Wextra;-Wno-unused-parameter>")
endif()
if(CEL_ENABLE_OPENCL)
  target_sources(test_gpu_abi PRIVATE "${_gen}/AbiOpenCLSource.cpp")
  target_compile_definitions(test_gpu_abi PRIVATE CEL_TEST_HAVE_OPENCL=1 CEL_HAVE_KHRONOS_CL_HEADERS=1)
  target_include_directories(test_gpu_abi PRIVATE "${CEL_GPU_OPENCL_INCLUDE_DIR}")
else()
  target_compile_definitions(test_gpu_abi PRIVATE CEL_TEST_HAVE_OPENCL=0)
  set_source_files_properties(test_env_opencl.cpp PROPERTIES HEADER_FILE_ONLY ON)
endif()
if(CEL_ENABLE_CUDA)
  find_package(CUDAToolkit REQUIRED)
  target_sources(test_gpu_abi PRIVATE abi/AbiCuda.cu)
  set_target_properties(test_gpu_abi PROPERTIES CUDA_ARCHITECTURES "${CEL_CUDA_ARCHITECTURES}"
                                                CUDA_RUNTIME_LIBRARY Static)
  target_compile_options(test_gpu_abi PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Wno-deprecated-gpu-targets>)
  target_compile_definitions(test_gpu_abi PRIVATE CEL_TEST_HAVE_CUDA=1)
  target_link_libraries(test_gpu_abi PRIVATE CUDA::cudart_static)
endif()
if(CEL_ENABLE_METAL)
  target_sources(test_gpu_abi PRIVATE test_env_metal.mm "${_gen}/AbiMetalSource.cpp")
  set_source_files_properties(test_env_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(test_gpu_abi PRIVATE CEL_TEST_HAVE_METAL=1)
  target_link_libraries(test_gpu_abi PRIVATE "-framework Metal" "-framework Foundation")
else()
  target_compile_definitions(test_gpu_abi PRIVATE CEL_TEST_HAVE_METAL=0)
endif()
add_test(NAME gpu_abi COMMAND test_gpu_abi)
# needs an OpenCL/CUDA/Metal device (POCL on the Linux workbench); exits 77 (= skipped) if none
set_tests_properties(gpu_abi PROPERTIES LABELS "gpu-device" SKIP_RETURN_CODE 77)

# --- offline syntax checks of the flattened sources ---------------------------------------------
set(_flat_cl "${_gen}/reafilm_abi.cl")
set(_flat_metal "${CEL_GPU_METAL_FLAT_SOURCE}" "${_gen}/reafilm_abi.metal")
if(CEL_ENABLE_OPENCL)
  list(APPEND _flat_cl "${CEL_GPU_OPENCL_FLAT_SOURCE}")
endif()
add_custom_target(celgpu_check_sources ALL DEPENDS celgpu_kernel_sources celgpu_test_kernel_sources)

find_program(CEL_CLANG NAMES clang clang-18 clang-17 clang-16 clang-15)
find_program(CEL_CLANGXX NAMES clang++ clang++-18 clang++-17 clang++-16 clang++-15)
if(CEL_CLANG AND CEL_ENABLE_OPENCL)
  add_test(NAME gpu_opencl_syntax
    COMMAND "${CMAKE_COMMAND}" -DCLANG=${CEL_CLANG} "-DFILES=${_flat_cl}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_opencl_syntax.cmake")
  # needs a clang with OpenCL C support (Linux workbench); runner toolchains vary
  set_tests_properties(gpu_opencl_syntax PROPERTIES LABELS "linux-only")
endif()
if(APPLE)
  add_test(NAME gpu_metal_xcrun
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/metal_syntax/check_metal.sh" ${_flat_metal})
elseif(CEL_CLANGXX)
  add_test(NAME gpu_metal_objc_mock
    COMMAND "${CMAKE_COMMAND}" -E env "CLANGXX=${CEL_CLANGXX}"
            "${CMAKE_CURRENT_SOURCE_DIR}/metal_syntax/check_mm_mock.sh" "${CEL_GPU_TEST_REPO}")
  add_test(NAME gpu_metal_shim
    COMMAND "${CMAKE_COMMAND}" -E env "CLANGXX=${CEL_CLANGXX}"
            "${CMAKE_CURRENT_SOURCE_DIR}/metal_syntax/check_metal_shim.sh" ${_flat_metal})
  set_tests_properties(gpu_metal_shim gpu_metal_objc_mock PROPERTIES LABELS "linux-only")
endif()

# --- production parity (real plans; needs core + engine) ------------------------------------------
# Linked against the real celcore/celtex targets when they exist (top-level build). For the
# standalone workbench before integration, CEL_GPU_PARITY_EXTRA_SOURCES may list sources that
# provide them (e.g. src/engine/*.cpp, src/texture/*.cpp and a core stub).
set(CEL_GPU_PARITY_EXTRA_SOURCES "" CACHE STRING "Development only: sources providing core/engine for test_gpu_parity")
if((TARGET celcore AND TARGET celtex) OR CEL_GPU_PARITY_EXTRA_SOURCES)
  add_executable(test_gpu_parity test_gpu_parity.cpp PerturbedKernels.cpp PerturbedKernels.h test_env_opencl.cpp test_env_cuda.cpp ${CEL_GPU_PARITY_EXTRA_SOURCES})
  target_link_libraries(test_gpu_parity PRIVATE celgpu Threads::Threads)
  if(TARGET celcore AND TARGET celtex)
    target_link_libraries(test_gpu_parity PRIVATE celtex celcore)
  endif()
  target_include_directories(test_gpu_parity PRIVATE ${CEL_GPU_KERNEL_INCLUDE_DIRS} "${CEL_GPU_TEST_REPO}/src")
  if(MSVC)
    target_compile_options(test_gpu_parity PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/wd4100>")
  else()
    target_compile_options(test_gpu_parity PRIVATE "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall;-Wextra;-Wno-unused-parameter>")
  endif()
  if(CEL_ENABLE_OPENCL)
    target_compile_definitions(test_gpu_parity PRIVATE CEL_TEST_HAVE_OPENCL=1 CEL_HAVE_KHRONOS_CL_HEADERS=1)
    target_include_directories(test_gpu_parity PRIVATE "${CEL_GPU_OPENCL_INCLUDE_DIR}")
  else()
    target_compile_definitions(test_gpu_parity PRIVATE CEL_TEST_HAVE_OPENCL=0)
  endif()
  if(CEL_ENABLE_CUDA)
    target_compile_definitions(test_gpu_parity PRIVATE CEL_TEST_HAVE_CUDA=1)
    target_link_libraries(test_gpu_parity PRIVATE CUDA::cudart_static)
  endif()
  if(CEL_ENABLE_METAL)
    target_sources(test_gpu_parity PRIVATE test_env_metal.mm)
    target_compile_definitions(test_gpu_parity PRIVATE CEL_TEST_HAVE_METAL=1)
    target_link_libraries(test_gpu_parity PRIVATE "-framework Metal" "-framework Foundation")
  endif()
  add_test(NAME gpu_parity COMMAND test_gpu_parity)
  set_tests_properties(gpu_parity PROPERTIES LABELS "gpu-device" SKIP_RETURN_CODE 77)
  # no device needed (runs in CI): CPU vs reference and the worst-case GPU math model
  # (PerturbedKernels.cpp) on every parity case
  add_test(NAME gpu_math_tolerance COMMAND test_gpu_parity --model)
endif()
