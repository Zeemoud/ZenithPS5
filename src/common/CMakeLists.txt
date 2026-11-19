include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <format>
int main() { return std::format(\"{}\", 1).size() == 1 ? 0 : 1; }
" ZENITH_HAS_STD_FORMAT)
if(NOT ZENITH_HAS_STD_FORMAT)
  message(FATAL_ERROR
    "The C++ standard library has no <format>. Use GCC 13+ or Clang 17+ (with libstdc++ 13+).")
endif()

add_library(zenith_common STATIC
  assert.cpp
  log.cpp
  assert.hpp
  log.hpp
  types.hpp)
add_library(zenith::common ALIAS zenith_common)

# Headers are included as "common/xxx.hpp" from anywhere in the project.
target_include_directories(zenith_common PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(zenith_common PUBLIC cxx_std_20)
zenith_set_warnings(zenith_common)
