# licensegen - license key tool (keygen / header / sign / verify). See README.md.
# Usage from the top-level CMakeLists.txt:  add_subdirectory(tools/licensegen)  (after src/license)
add_executable(licensegen licensegen.cpp)
target_link_libraries(licensegen PRIVATE cellic_sign cellic)
target_compile_features(licensegen PRIVATE cxx_std_17)
