add_library(zenith_memory STATIC
  address_space.cpp
  address_space.hpp)
add_library(zenith::memory ALIAS zenith_memory)

target_include_directories(zenith_memory PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(zenith_memory PUBLIC zenith::common zenith::platform)
zenith_set_warnings(zenith_memory)
