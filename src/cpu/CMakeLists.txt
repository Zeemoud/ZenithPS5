# Guest code runs natively on the host CPU, so the host must be x86-64.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  message(FATAL_ERROR
    "ZenithPS5 runs guest code natively and needs an x86-64 host "
    "(found ${CMAKE_SYSTEM_PROCESSOR}). ARM hosts would need a CPU translator, which is not written.")
endif()

add_library(zenith_cpu STATIC
  guest_call.cpp
  guest_call.hpp
  guest_stack.cpp
  guest_stack.hpp
  stub_table.cpp
  stub_table.hpp)
add_library(zenith::cpu ALIAS zenith_cpu)

target_include_directories(zenith_cpu PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(zenith_cpu PUBLIC zenith::common zenith::memory)
zenith_set_warnings(zenith_cpu)
