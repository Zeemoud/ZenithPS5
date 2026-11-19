cmake_minimum_required(VERSION 3.25)

project(ZenithPS5
  VERSION 0.0.1
  DESCRIPTION "PlayStation 5 emulator (HLE)"
  LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Use: cmake --preset debug")
endif()

# Language standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(ZENITH_BUILD_TESTS "Build the test suite" ON)
option(ZENITH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ZENITH_ENABLE_ASAN "Enable AddressSanitizer + UBSan" OFF)

# Local CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" "${CMAKE_SOURCE_DIR}/cmake/modules")
include(CompilerWarnings)

if(ZENITH_ENABLE_ASAN AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

if(ZENITH_BUILD_TESTS)
  enable_testing()
endif()

# Subprojects: enabled one by one as each milestone gets its first source file.
add_subdirectory(src/common)
# add_subdirectory(src/platform)
# add_subdirectory(src/memory)
# add_subdirectory(src/loader)
# add_subdirectory(src/debug)
# add_subdirectory(src/frontend/cli)
if(ZENITH_BUILD_TESTS)
  add_subdirectory(tests)
endif()
