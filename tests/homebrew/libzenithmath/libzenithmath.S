# A tiny shared library, linked by the real linker and loaded by ZenithPS5 from its file.
# Exports zenith_add and zenith_triple. internal_helper is local: it must not be exported.
    .text
    .globl zenith_add
    .type zenith_add, @function
zenith_add:
    lea (%rdi,%rsi), %rax
    ret

    .globl zenith_triple
    .type zenith_triple, @function
zenith_triple:
    lea (%rdi,%rdi,2), %rax
    ret

    .type internal_helper, @function
internal_helper:
    ret
