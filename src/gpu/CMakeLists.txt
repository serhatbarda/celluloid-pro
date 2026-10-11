# Reafilm Pro - GPU backends (owned by the gpu role). Usage from the top-level CMakeLists.txt:
#
#     add_subdirectory(src/gpu)          # defines the static library target `celgpu` (+ alias Reafilm::gpu)
#     target_link_libraries(<plugin> PRIVATE celgpu)
#
# Options (define them before add_subdirectory to override the defaults):
#     CEL_ENABLE_OPENCL   ON   OpenCL backend (runtime loaded dynamically; headers: system, CUDA
#                              toolkit, or Khronos OpenCL-Headers via FetchContent)
#     CEL_ENABLE_CUDA     OFF  CUDA backend (needs the CUDA toolkit / nvcc)
#     CEL_ENABLE_METAL    ON on Apple, OFF elsewhere (Apple only)
#     CEL_CUDA_ARCHITECTURES   default derived from the nvcc version (see below)
#     CEL_GPU_KERNEL_INCLUDE_DIRS  extra search dirs for kernel headers (searched after
#                              src/kernels; empty for product builds)
# See src/gpu/README.md.
cmake_minimum_required(VERSION 3.20)

option(CEL_ENABLE_OPENCL "Build the OpenCL GPU backend" ON)
option(CEL_ENABLE_CUDA "Build the CUDA GPU backend (requires nvcc)" OFF)
if(APPLE)
  option(CEL_ENABLE_METAL "Build the Metal GPU backend" ON)
else()
  set(CEL_ENABLE_METAL OFF CACHE BOOL "Build the Metal GPU backend (Apple only)" FORCE)
endif()
set(CEL_GPU_KERNEL_INCLUDE_DIRS "" CACHE STRING
    "Extra include dirs for kernel headers (searched after src/kernels; empty for product builds)")

get_filename_component(CEL_GPU_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)          # src/
get_filename_component(CEL_GPU_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)      # repo root
include("${CEL_GPU_REPO_DIR}/cmake/FlattenKernels.cmake")

set(CEL_GPU_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CEL_GPU_GEN_DIR}")

add_library(celgpu STATIC
  GpuBackends.h GpuCommon.h GpuCommon.cpp ProductionKernels.cpp Registry.cpp
  kernels/Probe.h kernels/EntryOpenCL.h kernels/EntryMetal.h kernels/EntryCuda.h)
add_library(Reafilm::gpu ALIAS celgpu)
target_compile_features(celgpu PUBLIC cxx_std_17)
target_include_directories(celgpu PUBLIC "${CEL_GPU_SRC_DIR}")
set_target_properties(celgpu PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)
if(MSVC)
  target_compile_options(celgpu PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/wd4100>")
  target_compile_definitions(celgpu PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(celgpu PRIVATE "$<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wall;-Wextra>")
endif()
if(CEL_GPU_KERNEL_INCLUDE_DIRS)
  # C++ TUs that include the kernels (probe reference) see the same headers as the device sources
  target_include_directories(celgpu PRIVATE ${CEL_GPU_KERNEL_INCLUDE_DIRS})
endif()

set(CEL_GPU_KERNEL_DEPEND_DIRS "${CEL_GPU_SRC_DIR}/kernels" "${CMAKE_CURRENT_SOURCE_DIR}/kernels")

# ---------------------------------------------------------------------------------- OpenCL ----
if(CEL_ENABLE_OPENCL)
  set(CEL_OPENCL_HEADERS_DIR "" CACHE PATH "Directory containing CL/cl.h (empty = auto)")
  set(_cel_cl_inc "${CEL_OPENCL_HEADERS_DIR}")
  if(NOT _cel_cl_inc)
    find_path(_cel_cl_found CL/cl.h
      HINTS ENV OpenCL_ROOT ENV CUDA_PATH
      PATH_SUFFIXES include)
    if(_cel_cl_found)
      set(_cel_cl_inc "${_cel_cl_found}")
    endif()
  endif()
  if(NOT _cel_cl_inc)
    include(FetchContent)
    # Khronos OpenCL-Headers (Apache-2.0), header only. SOURCE_SUBDIR points at a directory without
    # a CMakeLists.txt so only the sources are populated (no targets/tests from the dependency).
    FetchContent_Declare(cel_opencl_headers
      GIT_REPOSITORY https://github.com/KhronosGroup/OpenCL-Headers.git
      GIT_TAG v2024.10.24
      GIT_SHALLOW TRUE
      SOURCE_SUBDIR cel-no-cmake)
    FetchContent_MakeAvailable(cel_opencl_headers)
    set(_cel_cl_inc "${cel_opencl_headers_SOURCE_DIR}")
  endif()
  message(STATUS "Reafilm GPU: OpenCL backend ON (headers: ${_cel_cl_inc})")

  cel_embed_kernel_source(
    ROOT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/kernels/ProductionOpenCL.cl"
    SYMBOL cel_opencl_production
    OUTPUT_CPP "${CEL_GPU_GEN_DIR}/OpenCLProductionSource.cpp"
    OUTPUT_FLAT "${CEL_GPU_GEN_DIR}/reafilm_production.cl"
    BASE_DIR "${CEL_GPU_REPO_DIR}"
    INCLUDE_DIRS ${CEL_GPU_KERNEL_INCLUDE_DIRS}
    DEPENDS_DIRS ${CEL_GPU_KERNEL_DEPEND_DIRS})
  target_sources(celgpu PRIVATE
    opencl/CLApi.h opencl/CLApi.cpp opencl/OpenCLBackend.cpp
    "${CEL_GPU_GEN_DIR}/OpenCLProductionSource.cpp")
  set_source_files_properties(opencl/CLApi.cpp opencl/OpenCLBackend.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${_cel_cl_inc}")
  target_compile_definitions(celgpu PRIVATE CEL_GPU_HAVE_OPENCL=1 CEL_HAVE_KHRONOS_CL_HEADERS=1)
  target_link_libraries(celgpu PRIVATE ${CMAKE_DL_LIBS})
  set(CEL_GPU_OPENCL_FLAT_SOURCE "${CEL_GPU_GEN_DIR}/reafilm_production.cl" CACHE INTERNAL "")
  set(CEL_GPU_OPENCL_INCLUDE_DIR "${_cel_cl_inc}" CACHE INTERNAL "CL/cl.h directory used by celgpu")
endif()

# ------------------------------------------------------------------------------------ CUDA ----
if(CEL_ENABLE_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  set(_v "${CMAKE_CUDA_COMPILER_VERSION}")
  # SASS for every supported generation + PTX of the newest (JIT for future GPUs).
  if(_v VERSION_GREATER_EQUAL 13.0)
    # CUDA 13 dropped Maxwell/Pascal/Volta (sm_5x-sm_7.2)
    set(_cel_archs "75-real;80-real;86-real;89-real;90-real;120-real;120-virtual")
  elseif(_v VERSION_GREATER_EQUAL 12.8)
    set(_cel_archs "52-real;60-real;61-real;70-real;75-real;80-real;86-real;89-real;90-real;120-real;120-virtual")
  elseif(_v VERSION_GREATER_EQUAL 11.8)
    set(_cel_archs "52-real;60-real;61-real;70-real;75-real;80-real;86-real;89-real;90-real;90-virtual")
  else()
    set(_cel_archs "52-real;60-real;61-real;70-real;75-real;80-real;86-real;86-virtual")
  endif()
  set(CEL_CUDA_ARCHITECTURES "${_cel_archs}" CACHE STRING "CUDA architectures for the kernels")
  message(STATUS "Reafilm GPU: CUDA backend ON (nvcc ${_v}, archs ${CEL_CUDA_ARCHITECTURES})")
  target_sources(celgpu PRIVATE cuda/CudaKernels.cu cuda/CudaBackend.cpp)
  set_target_properties(celgpu PROPERTIES
    CUDA_ARCHITECTURES "${CEL_CUDA_ARCHITECTURES}"
    CUDA_RUNTIME_LIBRARY Static
    CUDA_SEPARABLE_COMPILATION OFF)
  # no fast math (CPU parity); silence the deprecation note for sm_5x/6x which we still ship
  target_compile_options(celgpu PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-Wno-deprecated-gpu-targets>)
  if(CEL_GPU_KERNEL_INCLUDE_DIRS)
    target_include_directories(celgpu PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:${CEL_GPU_KERNEL_INCLUDE_DIRS}>)
  endif()
  target_compile_definitions(celgpu PRIVATE CEL_GPU_HAVE_CUDA=1)
  target_link_libraries(celgpu PRIVATE CUDA::cudart_static)
endif()

# ----------------------------------------------------------------------------------- Metal ----
# The flattened MSL is generated on every platform (the Linux workbench syntax-checks it with a
# C++ shim, macOS CI with `xcrun metal`); it is compiled into the library only with Metal ON.
cel_embed_kernel_source(
  ROOT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/kernels/ProductionMetal.metal"
  SYMBOL cel_metal_production
  OUTPUT_CPP "${CEL_GPU_GEN_DIR}/MetalProductionSource.cpp"
  OUTPUT_FLAT "${CEL_GPU_GEN_DIR}/reafilm_production.metal"
  BASE_DIR "${CEL_GPU_REPO_DIR}"
  INCLUDE_DIRS ${CEL_GPU_KERNEL_INCLUDE_DIRS}
  DEPENDS_DIRS ${CEL_GPU_KERNEL_DEPEND_DIRS})
set(CEL_GPU_METAL_FLAT_SOURCE "${CEL_GPU_GEN_DIR}/reafilm_production.metal" CACHE INTERNAL "")
if(CEL_ENABLE_METAL)
  if(NOT APPLE)
    message(FATAL_ERROR "CEL_ENABLE_METAL requires Apple platforms")
  endif()
  enable_language(OBJCXX)
  target_sources(celgpu PRIVATE metal/MetalBackend.mm "${CEL_GPU_GEN_DIR}/MetalProductionSource.cpp")
  set_source_files_properties(metal/MetalBackend.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_compile_definitions(celgpu PRIVATE CEL_GPU_HAVE_METAL=1)
  target_link_libraries(celgpu PRIVATE "-framework Metal" "-framework Foundation")
  message(STATUS "Reafilm GPU: Metal backend ON")
endif()

# Flattened sources as standalone files (inspection, `xcrun metal` syntax check in macOS CI):
#   cmake --build <dir> --target celgpu_kernel_sources
set(_cel_flat "${CEL_GPU_GEN_DIR}/reafilm_production.metal")
if(CEL_ENABLE_OPENCL)
  list(APPEND _cel_flat "${CEL_GPU_GEN_DIR}/reafilm_production.cl")
endif()
add_custom_target(celgpu_kernel_sources DEPENDS ${_cel_flat})
