# Tiny freestanding guest programs, built with the host toolchain and run through the `zenith` CLI.
#
# They are linked as plain static ET_EXEC files at 0x400000, with 16 KiB segment alignment (the
# granularity of the guest address space) and separate segments for headers, code and data.
set(ZENITH_GUEST_LINK_FLAGS
  -nostdlib -static -no-pie
  -Wno-unused-command-line-argument
  -Wl,-Ttext-segment=0x400000
  -Wl,-z,max-page-size=0x4000
  -Wl,-z,separate-code)

function(zenith_add_homebrew name expected_output)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.S")
  set(elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")

  add_custom_command(
    OUTPUT "${elf}"
    COMMAND "${CMAKE_C_COMPILER}" ${ZENITH_GUEST_LINK_FLAGS} -o "${elf}" "${source}"
    DEPENDS "${source}"
    COMMENT "Building guest program ${name}"
    VERBATIM)
  add_custom_target(homebrew_${name} ALL DEPENDS "${elf}")

  add_test(NAME homebrew.${name} COMMAND zenith "${elf}")
  set_tests_properties(homebrew.${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expected_output}")
endfunction()

zenith_add_homebrew(return42 "guest returned 42")
zenith_add_homebrew(data_bss "guest returned 42")
zenith_add_homebrew(hello "Hello from the guest!")
