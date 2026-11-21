# Tiny freestanding guest programs, built with the host toolchain and run through the `zenith` CLI.
#
# Fixed-address programs are linked as plain static ET_EXEC files at 0x400000, position-independent ones
# (PIE) as static ET_DYN files, with no dynamic linker. Both use 16 KiB segment alignment (the granularity
# of the guest address space) and separate segments for headers, code and data.
set(ZENITH_GUEST_COMMON_FLAGS
  -nostdlib
  -Wno-unused-command-line-argument
  -Wl,-z,max-page-size=0x4000
  -Wl,-z,separate-code)

set(ZENITH_GUEST_FIXED_FLAGS ${ZENITH_GUEST_COMMON_FLAGS} -static -no-pie -Wl,-Ttext-segment=0x400000)
set(ZENITH_GUEST_PIE_FLAGS ${ZENITH_GUEST_COMMON_FLAGS} -static-pie -Wl,--no-dynamic-linker)

# zenith_add_homebrew(<name> <expected output regex> [PIE] [EXPECT_CRASH])
function(zenith_add_homebrew name expected_output)
  cmake_parse_arguments(ARG "PIE;EXPECT_CRASH" "" "" ${ARGN})

  set(source "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.S")
  set(elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")

  if(ARG_PIE)
    set(link_flags ${ZENITH_GUEST_PIE_FLAGS})
  else()
    set(link_flags ${ZENITH_GUEST_FIXED_FLAGS})
  endif()

  add_custom_command(
    OUTPUT "${elf}"
    COMMAND "${CMAKE_C_COMPILER}" ${link_flags} -o "${elf}" "${source}"
    DEPENDS "${source}"
    COMMENT "Building guest program ${name}"
    VERBATIM)
  add_custom_target(homebrew_${name} ALL DEPENDS "${elf}")

  if(ARG_EXPECT_CRASH)
    # The program is expected to die from a signal: run it through sh so the test only checks the report.
    add_test(NAME homebrew.${name} COMMAND sh -c "\"$<TARGET_FILE:zenith>\" \"${elf}\"; exit 0")
  else()
    add_test(NAME homebrew.${name} COMMAND zenith "${elf}")
  endif()
  set_tests_properties(homebrew.${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expected_output}")
endfunction()

zenith_add_homebrew(return42 "guest returned 42")
zenith_add_homebrew(data_bss "guest returned 42")
zenith_add_homebrew(hello "Hello from the guest!")
zenith_add_homebrew(hello_pie "Hello from a relocated guest!" PIE)
zenith_add_homebrew(crash_null "null pointer dereference" EXPECT_CRASH)
