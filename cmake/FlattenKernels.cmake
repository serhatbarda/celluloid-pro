# Celluloid Pro - kernel source flattener (owned by the gpu role).
#
# The runtime-compiled GPU backends (OpenCL, Metal) need the portable kernel sources as ONE string.
# This file both
#   (a) provides the CMake function `cel_embed_kernel_source(...)` (include() it at configure time),
#   (b) is the build-time script that does the work (`cmake -P FlattenKernels.cmake` with -D args).
#
# Flattening resolves every LOCAL include  (#include "file")  textually, recursively:
#   - search order: directory of the including file, then INCLUDE_DIRS (in order)
#   - system includes (#include <...>) are left untouched (e.g. <metal_stdlib> inside #if branches)
#   - a file guarded by a classic include guard (#ifndef G / #define G as its first two directives)
#     or by #pragma once is inlined only once - exactly what the device compiler's preprocessor
#     would do. Unguarded files (X-macro lists such as KernelList.h) are inlined at every include.
#   - conditional compilation is NOT evaluated: an include inside an inactive #if branch is still
#     inlined (it stays inside the inactive branch, so the result is identical). A missing local
#     include is a hard build error.
#   - `#line N "src/kernels/File.h"` markers keep device compiler diagnostics pointing at the
#     original files (paths are relative to BASE_DIR, so no build-machine paths leak into binaries).
#   - include cycles are a hard error.
#
# Outputs: the flattened source (OUTPUT_FLAT, also used by the macOS `xcrun metal` syntax check)
# and a C++ file (OUTPUT_CPP) defining, in namespace cel::gpu::embedded:
#     extern const char        <SYMBOL>[];          // NUL-terminated source
#     extern const std::size_t <SYMBOL>_len;        // strlen
#     extern const char        <SYMBOL>_sha256[];   // hex SHA-256 of the source (cache keys, logs)
# The source is emitted as a byte array (MSVC limits string literals to 64 KiB). Outputs are only
# rewritten when their content changes.

cmake_minimum_required(VERSION 3.20)

# ------------------------------------------------------------------------------------------------
# Script mode
# ------------------------------------------------------------------------------------------------
if(CMAKE_SCRIPT_MODE_FILE AND NOT CEL_FLATTEN_AS_LIBRARY)

  function(_cel_count_lines text out)
    string(REGEX MATCHALL "\n" _nl "${text}")
    list(LENGTH _nl _n)
    set(${out} ${_n} PARENT_SCOPE)
  endfunction()

  function(_cel_rel_name path out)
    file(TO_CMAKE_PATH "${path}" _p)
    if(CEL_BASE_DIR)
      file(RELATIVE_PATH _r "${CEL_BASE_DIR}" "${_p}")
      if(NOT _r MATCHES "^\\.\\./")
        set(${out} "${_r}" PARENT_SCOPE)
        return()
      endif()
    endif()
    get_filename_component(_n "${_p}" NAME)
    set(${out} "${_n}" PARENT_SCOPE)
  endfunction()

  # Detect a classic include guard / pragma once. Sets out to the guard name, "#once#" or "".
  function(_cel_guard_of content out)
    set(${out} "" PARENT_SCOPE)
    if("\n${content}" MATCHES "\n[ \t]*#[ \t]*pragma[ \t]+once")
      set(${out} "#once#" PARENT_SCOPE)
      return()
    endif()
    string(REGEX MATCHALL "\n[ \t]*#[ \t]*[a-z]+[ \t]*[A-Za-z_0-9]*" _dirs "\n${content}")
    list(LENGTH _dirs _nd)
    if(_nd LESS 2)
      return()
    endif()
    list(GET _dirs 0 _d0)
    list(GET _dirs 1 _d1)
    if(_d0 MATCHES "#[ \t]*ifndef[ \t]+([A-Za-z_0-9]+)$")
      set(_g "${CMAKE_MATCH_1}")
      if(_d1 MATCHES "#[ \t]*define[ \t]+([A-Za-z_0-9]+)$" AND "${CMAKE_MATCH_1}" STREQUAL "${_g}")
        set(${out} "${_g}" PARENT_SCOPE)
      endif()
    endif()
  endfunction()

  # Recursively flatten `file`. Uses globals: CEL_INCLUDE_DIRS, CEL_SEEN_GUARDS (property),
  # CEL_STACK (property), CEL_ALL_FILES (property).
  function(_cel_flatten file out)
    file(TO_CMAKE_PATH "${file}" file)
    get_property(_stack GLOBAL PROPERTY CEL_STACK)
    list(FIND _stack "${file}" _onstack)
    if(NOT _onstack EQUAL -1)
      message(FATAL_ERROR "FlattenKernels: include cycle through ${file}")
    endif()
    if(NOT EXISTS "${file}")
      message(FATAL_ERROR "FlattenKernels: file not found: ${file}")
    endif()
    file(READ "${file}" content)
    string(REPLACE "\r" "" content "${content}")
    if(NOT content MATCHES "\n$")
      string(APPEND content "\n")
    endif()
    set_property(GLOBAL APPEND PROPERTY CEL_ALL_FILES "${file}")

    _cel_guard_of("${content}" guard)
    if(guard)
      if(guard STREQUAL "#once#")
        set(guard "#once#${file}")
      endif()
      get_property(_seen GLOBAL PROPERTY CEL_SEEN_GUARDS)
      list(FIND _seen "${guard}" _g)
      if(NOT _g EQUAL -1)
        _cel_rel_name("${file}" _rel)
        set(${out} "/* (${_rel}: already included) */\n" PARENT_SCOPE)
        return()
      endif()
      set_property(GLOBAL APPEND PROPERTY CEL_SEEN_GUARDS "${guard}")
    endif()
    # `#pragma once` is meaningless (and warned about) in the single flattened file: neutralise it
    # (same line count, so #line markers stay exact)
    string(REGEX REPLACE "(^|\n)[ \t]*#[ \t]*pragma[ \t]+once[^\n]*" "\\1/* #pragma once */" content "${content}")

    set_property(GLOBAL APPEND PROPERTY CEL_STACK "${file}")
    get_filename_component(dir "${file}" DIRECTORY)
    _cel_rel_name("${file}" rel)

    set(result "")
    if(CEL_LINE_MARKERS)
      set(result "#line 1 \"${rel}\"\n")
    endif()
    set(rest "\n${content}")     # leading \n so a directive on line 1 matches the pattern
    set(line 0)                  # number of lines of `content` already consumed into result
    while(TRUE)
      string(REGEX MATCH "\n[ \t]*#[ \t]*include[ \t]*\"[^\"\n]+\"[^\n]*\n" m "${rest}")
      if(NOT m)
        break()
      endif()
      string(FIND "${rest}" "${m}" pos)
      string(SUBSTRING "${rest}" 0 ${pos} before)
      string(LENGTH "${m}" mlen)
      math(EXPR after_pos "${pos} + ${mlen}")
      string(SUBSTRING "${rest}" ${after_pos} -1 rest)
      set(rest "\n${rest}")      # keep the newline that terminated the directive as a separator
      # `before` is "" (directive directly follows the previous one / starts the file) or
      # "\n" + the lines before the directive (the \n is the separator, not content)
      set(_bl 0)
      if(NOT before STREQUAL "")
        string(SUBSTRING "${before}" 1 -1 before)
        string(APPEND result "${before}\n")
        _cel_count_lines("${before}\n" _bl)
      endif()
      math(EXPR line "${line} + ${_bl} + 1")   # +1 for the directive line itself
      string(REGEX MATCH "\"([^\"]+)\"" _q "${m}")
      set(inc "${CMAKE_MATCH_1}")
      set(found "")
      foreach(d IN LISTS dir CEL_INCLUDE_DIRS)
        if(EXISTS "${d}/${inc}" AND NOT IS_DIRECTORY "${d}/${inc}")
          get_filename_component(found "${d}/${inc}" ABSOLUTE)
          break()
        endif()
      endforeach()
      if(NOT found)
        message(FATAL_ERROR "FlattenKernels: ${rel}: cannot resolve #include \"${inc}\" "
                            "(searched ${dir};${CEL_INCLUDE_DIRS})")
      endif()
      _cel_flatten("${found}" sub)
      string(APPEND result "${sub}")
      if(CEL_LINE_MARKERS)
        math(EXPR next "${line} + 1")
        string(APPEND result "#line ${next} \"${rel}\"\n")
      endif()
    endwhile()
    string(SUBSTRING "${rest}" 1 -1 rest)
    string(APPEND result "${rest}")

    get_property(_stack GLOBAL PROPERTY CEL_STACK)
    list(REMOVE_AT _stack -1)
    set_property(GLOBAL PROPERTY CEL_STACK "${_stack}")
    set(${out} "${result}" PARENT_SCOPE)
  endfunction()

  function(_cel_write_if_different path content)
    if(EXISTS "${path}")
      file(READ "${path}" old)
      if(old STREQUAL content)
        return()
      endif()
    endif()
    file(WRITE "${path}.tmp" "${content}")
    file(RENAME "${path}.tmp" "${path}")
  endfunction()

  foreach(v CEL_ROOT_FILE CEL_OUTPUT_FLAT CEL_OUTPUT_CPP CEL_SYMBOL)
    if(NOT ${v})
      message(FATAL_ERROR "FlattenKernels: -D${v}=... is required")
    endif()
  endforeach()
  if(NOT DEFINED CEL_LINE_MARKERS)
    set(CEL_LINE_MARKERS ON)
  endif()
  string(REPLACE "|" ";" CEL_INCLUDE_DIRS "${CEL_INCLUDE_DIRS}")
  set_property(GLOBAL PROPERTY CEL_STACK "")
  set_property(GLOBAL PROPERTY CEL_SEEN_GUARDS "")
  set_property(GLOBAL PROPERTY CEL_ALL_FILES "")

  _cel_rel_name("${CEL_ROOT_FILE}" _rootrel)
  _cel_flatten("${CEL_ROOT_FILE}" flat)
  set(flat "/* Celluloid Pro - GENERATED flattened kernel source (${CEL_SYMBOL}) from ${_rootrel}. Do not edit. */\n${flat}")
  _cel_write_if_different("${CEL_OUTPUT_FLAT}" "${flat}")

  string(SHA256 sha "${flat}")
  string(LENGTH "${flat}" len)
  # Byte array: read back the file we just wrote as hex (exact bytes, independent of CMake escaping).
  file(READ "${CEL_OUTPUT_FLAT}" hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
  # wrap lines every 24 bytes (5 chars each) to keep the generated file editor/compiler friendly
  string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){24})" "\\1\n" hex "${hex}")
  get_property(all GLOBAL PROPERTY CEL_ALL_FILES)
  list(REMOVE_DUPLICATES all)
  set(deps "")
  foreach(f IN LISTS all)
    _cel_rel_name("${f}" r)
    string(APPEND deps "//   ${r}\n")
  endforeach()
  set(cpp "// Celluloid Pro - GENERATED by cmake/FlattenKernels.cmake. Do not edit.\n")
  string(APPEND cpp "// Root: ${_rootrel}\n// Files:\n${deps}")
  string(APPEND cpp "#include <cstddef>\nnamespace cel { namespace gpu { namespace embedded {\n")
  string(APPEND cpp "extern const char ${CEL_SYMBOL}[];\nextern const std::size_t ${CEL_SYMBOL}_len;\n")
  string(APPEND cpp "extern const char ${CEL_SYMBOL}_sha256[];\n")
  string(APPEND cpp "const char ${CEL_SYMBOL}[] = {\n${hex}0x00 };\n")
  string(APPEND cpp "const std::size_t ${CEL_SYMBOL}_len = ${len};\n")
  string(APPEND cpp "const char ${CEL_SYMBOL}_sha256[] = \"${sha}\";\n")
  string(APPEND cpp "}}} // namespace cel::gpu::embedded\n")
  _cel_write_if_different("${CEL_OUTPUT_CPP}" "${cpp}")
  return()
endif()

# ------------------------------------------------------------------------------------------------
# Configure-time API
# ------------------------------------------------------------------------------------------------
set(_CEL_FLATTEN_SCRIPT "${CMAKE_CURRENT_LIST_FILE}")

# cel_embed_kernel_source(
#     ROOT_FILE    <file to flatten>
#     SYMBOL       <C identifier of the embedded string>
#     OUTPUT_CPP   <generated .cpp>      (add it to a target's sources)
#     OUTPUT_FLAT  <generated flattened source file>
#     [BASE_DIR    <dir; #line paths are relative to it>]          default: CMAKE_SOURCE_DIR
#     [INCLUDE_DIRS <dir>...]            extra search dirs for "..." includes
#     [DEPENDS_DIRS <dir>...]            dirs whose *.h/*.inc/*.cl/*.metal changes trigger regeneration
#     [NO_LINE_MARKERS])
function(cel_embed_kernel_source)
  cmake_parse_arguments(A "NO_LINE_MARKERS" "ROOT_FILE;SYMBOL;OUTPUT_CPP;OUTPUT_FLAT;BASE_DIR"
                        "INCLUDE_DIRS;DEPENDS_DIRS" ${ARGN})
  foreach(v ROOT_FILE SYMBOL OUTPUT_CPP OUTPUT_FLAT)
    if(NOT A_${v})
      message(FATAL_ERROR "cel_embed_kernel_source: ${v} is required")
    endif()
  endforeach()
  if(NOT A_BASE_DIR)
    set(A_BASE_DIR "${CMAKE_SOURCE_DIR}")
  endif()
  get_filename_component(_root "${A_ROOT_FILE}" ABSOLUTE)
  get_filename_component(_rootdir "${_root}" DIRECTORY)
  set(_globs "")
  foreach(d IN LISTS _rootdir A_INCLUDE_DIRS A_DEPENDS_DIRS)
    list(APPEND _globs "${d}/*.h" "${d}/*.inc" "${d}/*.cl" "${d}/*.metal")
  endforeach()
  file(GLOB _deps CONFIGURE_DEPENDS ${_globs})
  string(REPLACE ";" "|" _incs "${A_INCLUDE_DIRS}")
  set(_markers ON)
  if(A_NO_LINE_MARKERS)
    set(_markers OFF)
  endif()
  get_filename_component(_rel "${A_OUTPUT_FLAT}" NAME)
  add_custom_command(
    OUTPUT "${A_OUTPUT_CPP}" "${A_OUTPUT_FLAT}"
    COMMAND "${CMAKE_COMMAND}"
            "-DCEL_ROOT_FILE=${_root}"
            "-DCEL_SYMBOL=${A_SYMBOL}"
            "-DCEL_OUTPUT_CPP=${A_OUTPUT_CPP}"
            "-DCEL_OUTPUT_FLAT=${A_OUTPUT_FLAT}"
            "-DCEL_BASE_DIR=${A_BASE_DIR}"
            "-DCEL_INCLUDE_DIRS=${_incs}"
            "-DCEL_LINE_MARKERS=${_markers}"
            -P "${_CEL_FLATTEN_SCRIPT}"
    DEPENDS ${_deps} "${_CEL_FLATTEN_SCRIPT}"
    COMMENT "Flattening kernel source ${_rel}"
    VERBATIM)
endfunction()
