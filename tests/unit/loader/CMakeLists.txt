add_executable(zenith_loader_tests
  elf_builder.hpp
  test_elf_file.cpp
  test_elf_loader.cpp)
target_link_libraries(zenith_loader_tests PRIVATE zenith::loader GTest::gtest_main)
zenith_set_warnings(zenith_loader_tests)
gtest_discover_tests(zenith_loader_tests)
