add_executable(zenith_memory_tests
  test_address_space.cpp)
target_link_libraries(zenith_memory_tests PRIVATE zenith::memory GTest::gtest_main)
zenith_set_warnings(zenith_memory_tests)
gtest_discover_tests(zenith_memory_tests)
