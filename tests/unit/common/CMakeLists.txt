add_executable(zenith_common_tests
  test_assert.cpp
  test_log.cpp
  test_types.cpp)
target_link_libraries(zenith_common_tests PRIVATE zenith::common GTest::gtest_main)
zenith_set_warnings(zenith_common_tests)
gtest_discover_tests(zenith_common_tests)
