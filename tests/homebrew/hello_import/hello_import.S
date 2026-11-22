# Hello world with named imports, called through the PLT (R_X86_64_JUMP_SLOT relocations).
# Linked against libzenithtest.so: the loader binds zenith_write and zenith_exit to host functions.
    .section .rodata
message:
    .ascii "Hello through a named import!\n"
    .set message_length, . - message

    .text
    .globl _start
_start:
    sub $8, %rsp
    mov $1, %edi
    lea message(%rip), %rsi
    mov $message_length, %edx
    call zenith_write@PLT
    xor %edi, %edi
    call zenith_exit@PLT
