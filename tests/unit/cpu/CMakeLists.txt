add_executable(zenith_cpu_tests
  test_crash_handler.cpp
  test_guest_call.cpp
  test_guest_stack.cpp
  test_stub_table.cpp)
target_link_libraries(zenith_cpu_tests PRIVATE zenith::cpu GTest::gtest_main)
zenith_set_warnings(zenith_cpu_tests)
gtest_discover_tests(zenith_cpu_tests)
