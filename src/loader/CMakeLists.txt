add_library(zenith_loader STATIC
  elf/elf_file.cpp
  elf/elf_file.hpp
  elf/elf_loader.cpp
  elf/elf_loader.hpp
  elf/elf_types.hpp)
add_library(zenith::loader ALIAS zenith_loader)

target_include_directories(zenith_loader PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(zenith_loader PUBLIC zenith::common zenith::memory)
zenith_set_warnings(zenith_loader)
