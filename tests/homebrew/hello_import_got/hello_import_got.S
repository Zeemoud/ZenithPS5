# Same as hello_import, but calling through the GOT (R_X86_64_GLOB_DAT relocations).
    .section .rodata
message:
    .ascii "Hello through a GOT import!\n"
    .set message_length, . - message

    .text
    .globl _start
_start:
    sub $8, %rsp
    mov $1, %edi
    lea message(%rip), %rsi
    mov $message_length, %edx
    call *zenith_write@GOTPCREL(%rip)
    xor %edi, %edi
    call *zenith_exit@GOTPCREL(%rip)
