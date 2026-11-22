# Link-time stand-in for the host's test library "libzenithtest.so".
#
# Programs are linked against it so that the linker records real imports (symbol names, DT_NEEDED). It is never
# loaded: ZenithPS5 provides the library itself, as host functions (see src/core/bridge.cpp).
# "zenith_unimplemented_demo" is deliberately NOT implemented by the host: it exists to test the report that
# names a missing function.
    .text
    .globl zenith_write
    .type zenith_write, @function
zenith_write:
    ret

    .globl zenith_exit
    .type zenith_exit, @function
zenith_exit:
    ret

    .globl zenith_unimplemented_demo
    .type zenith_unimplemented_demo, @function
zenith_unimplemented_demo:
    ret
