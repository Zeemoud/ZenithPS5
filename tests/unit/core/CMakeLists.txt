add_executable(zenith_core_tests
  test_file.cpp
  test_run_elf.cpp)
target_include_directories(zenith_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../loader")  # elf_builder.hpp
target_link_libraries(zenith_core_tests PRIVATE zenith::core GTest::gtest_main)
zenith_set_warnings(zenith_core_tests)
gtest_discover_tests(zenith_core_tests)
