# Celluloid Pro - colour science core library (owned by core).
# Usable via add_subdirectory(src/core) from the top-level project; defines target `celcore`.
if(NOT TARGET celcore)
  add_library(celcore STATIC
    Core.cpp
    Spectral.cpp
    Stocks.cpp
    Bake.cpp)
  target_include_directories(celcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../kernels)
  target_compile_features(celcore PUBLIC cxx_std_17)
  set_target_properties(celcore PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
  find_package(Threads REQUIRED)
  target_link_libraries(celcore PUBLIC Threads::Threads)
  if(MSVC)
    # std::filesystem::u8path is deprecated only in C++20; keep the UTF-8 path API quiet on MSVC
    target_compile_definitions(celcore PRIVATE _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS)
  endif()
endif()
