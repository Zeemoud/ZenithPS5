# Tiny freestanding guest programs, built with the host toolchain and run through the `zenith` CLI.
#
# Fixed-address programs are linked as plain static ET_EXEC files at 0x400000, position-independent ones
# (PIE) as static ET_DYN files, with no dynamic linker. Programs that use imports are linked against a stand-in
# for the host's test library (libzenithtest.so), so the linker records real imports and DT_NEEDED.
# All use 16 KiB segment alignment (the granularity of the guest address space) and separate segments for
# headers, code and data.
set(ZENITH_GUEST_COMMON_FLAGS
  -nostdlib
  -Wno-unused-command-line-argument
  -Wl,-z,max-page-size=0x4000
  -Wl,-z,separate-code)

set(ZENITH_GUEST_FIXED_FLAGS ${ZENITH_GUEST_COMMON_FLAGS} -static -no-pie -Wl,-Ttext-segment=0x400000)
set(ZENITH_GUEST_PIE_FLAGS ${ZENITH_GUEST_COMMON_FLAGS} -static-pie -Wl,--no-dynamic-linker)
set(ZENITH_GUEST_IMPORT_FLAGS ${ZENITH_GUEST_COMMON_FLAGS} -pie -Wl,--no-dynamic-linker)

# The stand-in for libzenithtest.so (symbol names only: the host provides the code).
set(ZENITH_TEST_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/libzenithtest.so")
set(ZENITH_TEST_LIBRARY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/libzenithtest/libzenithtest.S")
add_custom_command(
  OUTPUT "${ZENITH_TEST_LIBRARY}"
  COMMAND "${CMAKE_C_COMPILER}" -nostdlib -shared -Wno-unused-command-line-argument
          -Wl,-soname,libzenithtest.so -o "${ZENITH_TEST_LIBRARY}" "${ZENITH_TEST_LIBRARY_SOURCE}"
  DEPENDS "${ZENITH_TEST_LIBRARY_SOURCE}"
  COMMENT "Building the link-time stand-in for libzenithtest.so"
  VERBATIM)
add_custom_target(homebrew_libzenithtest ALL DEPENDS "${ZENITH_TEST_LIBRARY}")

# zenith_add_guest_library(<name> [IMPORTS])
# Builds <name>.so from <name>/<name>.S, a real shared library that ZenithPS5 loads from its file. With IMPORTS it
# is linked against the stand-in for libzenithtest.so, so its own imports have real names.
function(zenith_add_guest_library name)
  cmake_parse_arguments(ARG "IMPORTS" "" "" ${ARGN})

  set(source "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.S")
  set(library "${CMAKE_CURRENT_BINARY_DIR}/${name}.so")
  set(inputs "${source}")
  set(depends "${source}")
  if(ARG_IMPORTS)
    list(APPEND inputs "${ZENITH_TEST_LIBRARY}")
    list(APPEND depends "${ZENITH_TEST_LIBRARY}")
  endif()

  add_custom_command(
    OUTPUT "${library}"
    COMMAND "${CMAKE_C_COMPILER}" ${ZENITH_GUEST_COMMON_FLAGS} -shared -Wl,--hash-style=gnu
            -Wl,-soname,${name}.so -o "${library}" ${inputs}
    DEPENDS ${depends}
    COMMENT "Building guest library ${name}"
    VERBATIM)
  add_custom_target(homebrew_${name} ALL DEPENDS "${library}")
endfunction()

# zenith_add_homebrew(<name> <expected output regex> [PIE] [IMPORTS] [EXPECT_CRASH] [LIBRARIES <lib>...])
# LIBRARIES lists guest libraries (see above) the program is linked against; ZenithPS5 finds them next to it.
function(zenith_add_homebrew name expected_output)
  cmake_parse_arguments(ARG "PIE;IMPORTS;EXPECT_CRASH" "" "LIBRARIES" ${ARGN})

  set(source "${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.S")
  set(elf "${CMAKE_CURRENT_BINARY_DIR}/${name}.elf")

  set(inputs "${source}")
  set(depends "${source}")
  foreach(library IN LISTS ARG_LIBRARIES)
    list(APPEND inputs "${CMAKE_CURRENT_BINARY_DIR}/${library}.so")
    list(APPEND depends "${CMAKE_CURRENT_BINARY_DIR}/${library}.so")
  endforeach()
  if(ARG_IMPORTS)
    list(APPEND inputs "${ZENITH_TEST_LIBRARY}")
    list(APPEND depends "${ZENITH_TEST_LIBRARY}")
  endif()

  if(ARG_IMPORTS OR ARG_LIBRARIES)
    set(link_flags ${ZENITH_GUEST_IMPORT_FLAGS})
  elseif(ARG_PIE)
    set(link_flags ${ZENITH_GUEST_PIE_FLAGS})
  else()
    set(link_flags ${ZENITH_GUEST_FIXED_FLAGS})
  endif()

  add_custom_command(
    OUTPUT "${elf}"
    COMMAND "${CMAKE_C_COMPILER}" ${link_flags} -o "${elf}" ${inputs}
    DEPENDS ${depends}
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

zenith_add_guest_library(libzenithmath)
zenith_add_guest_library(libzenithgreet IMPORTS)

zenith_add_homebrew(return42 "guest returned 42")
zenith_add_homebrew(data_bss "guest returned 42")
zenith_add_homebrew(hello "Hello from the guest!")
zenith_add_homebrew(hello_pie "Hello from a relocated guest!" PIE)
zenith_add_homebrew(hello_import "Hello through a named import!" IMPORTS)
zenith_add_homebrew(hello_import_got "Hello through a GOT import!" IMPORTS)
zenith_add_homebrew(import_missing "unimplemented function 'zenith_unimplemented_demo'" IMPORTS)
zenith_add_homebrew(call_library "guest returned 42" IMPORTS LIBRARIES libzenithmath)
zenith_add_homebrew(hello_library "Hello from a library!" IMPORTS LIBRARIES libzenithgreet)
zenith_add_homebrew(crash_null "null pointer dereference" EXPECT_CRASH)
