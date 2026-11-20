# Behavior of the `zenith` command line (a test passes when the output matches).
add_test(NAME cli.help COMMAND zenith --help)
set_tests_properties(cli.help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: zenith")

add_test(NAME cli.no-arguments COMMAND zenith)
set_tests_properties(cli.no-arguments PROPERTIES PASS_REGULAR_EXPRESSION "no program given")

add_test(NAME cli.unknown-option COMMAND zenith --bogus)
set_tests_properties(cli.unknown-option PROPERTIES PASS_REGULAR_EXPRESSION "unknown option")

add_test(NAME cli.bad-log-level COMMAND zenith --log-level=loud program.elf)
set_tests_properties(cli.bad-log-level PROPERTIES PASS_REGULAR_EXPRESSION "unknown log level")

add_test(NAME cli.missing-file COMMAND zenith "${CMAKE_CURRENT_BINARY_DIR}/does-not-exist.elf")
set_tests_properties(cli.missing-file PROPERTIES PASS_REGULAR_EXPRESSION "cannot read")

add_test(NAME cli.not-an-elf COMMAND zenith "${PROJECT_SOURCE_DIR}/CMakeLists.txt")
set_tests_properties(cli.not-an-elf PROPERTIES PASS_REGULAR_EXPRESSION "not an ELF")
