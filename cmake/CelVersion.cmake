# Celluloid Pro - build identification.
#   CEL_BUILD_ID   : -DCEL_BUILD_ID=... (CI passes the commit), else the git commit of the source tree, else "dev"
#   CEL_BUILD_DATE : UTC date of the configure run (honours SOURCE_DATE_EPOCH for reproducible builds)
if(NOT CEL_BUILD_ID)
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=10 HEAD
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    OUTPUT_VARIABLE _cel_git_id OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _cel_git_rc ERROR_QUIET)
    if(_cel_git_rc EQUAL 0 AND _cel_git_id)
      set(CEL_BUILD_ID "${_cel_git_id}")
    endif()
  endif()
endif()
if(NOT CEL_BUILD_ID)
  set(CEL_BUILD_ID "dev")
endif()
string(TIMESTAMP CEL_BUILD_DATE "%Y-%m-%d" UTC)
