# Celluloid Pro - writes the build identification header at BUILD time (cmake -P script), so the
# About string always names the commit that was actually built (a configure-time value goes stale
# as soon as the tree moves on). Inputs: SRC (source dir), OUT (header path), FORCED (optional
# -DCEL_BUILD_ID given to CMake, e.g. by CI). The file is rewritten only when its content changes.
set(_id "${FORCED}")
if(NOT _id)
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS "${SRC}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=10 HEAD WORKING_DIRECTORY "${SRC}"
                    OUTPUT_VARIABLE _id OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc ERROR_QUIET)
    if(NOT _rc EQUAL 0)
      set(_id "")
    endif()
    if(_id)
      execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no WORKING_DIRECTORY "${SRC}"
                      OUTPUT_VARIABLE _dirty OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(_dirty)
        set(_id "${_id}+")
      endif()
    endif()
  endif()
endif()
if(NOT _id)
  set(_id "dev")
endif()
string(TIMESTAMP _date "%Y-%m-%d" UTC)
set(_txt "// GENERATED at build time by cmake/CelBuildIdScript.cmake - do not edit.\n#pragma once\n#define CEL_BUILD_ID   \"${_id}\"\n#define CEL_BUILD_DATE \"${_date}\"\n")
set(_old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" _old)
endif()
if(NOT _old STREQUAL _txt)
  file(WRITE "${OUT}" "${_txt}")
endif()
