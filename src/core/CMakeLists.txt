add_library(zenith_core STATIC
  bridge.cpp
  bridge.hpp
  file.cpp
  file.hpp
  run_elf.cpp
  run_elf.hpp)
add_library(zenith::core ALIAS zenith_core)

target_include_directories(zenith_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(zenith_core PUBLIC zenith::common zenith::memory zenith::loader zenith::cpu)
zenith_set_warnings(zenith_core)
