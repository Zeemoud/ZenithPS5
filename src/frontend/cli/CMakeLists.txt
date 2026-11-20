add_executable(zenith main.cpp)
target_link_libraries(zenith PRIVATE zenith::core)
set_target_properties(zenith PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
zenith_set_warnings(zenith)
