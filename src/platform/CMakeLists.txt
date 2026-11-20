if(NOT (UNIX AND NOT APPLE))
  message(FATAL_ERROR
    "ZenithPS5 currently supports Linux only (the Windows and macOS backends are not written yet).")
endif()

add_library(zenith_platform STATIC
  linux/virtual_memory.cpp
  virtual_memory.hpp)
add_library(zenith::platform ALIAS zenith_platform)

target_include_directories(zenith_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(zenith_platform PUBLIC zenith::common)
zenith_set_warnings(zenith_platform)
