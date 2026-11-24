# A shared library that uses the host: zenith_greet prints a message through the import zenith_write
# (linked against the stand-in for libzenithtest.so, so the import has a real name and DT_NEEDED).
    .section .rodata
message:
    .ascii "Hello from a library!\n"
    .set message_length, . - message

    .text
    .globl zenith_greet
    .type zenith_greet, @function
zenith_greet:
    sub $8, %rsp
    mov $1, %edi
    lea message(%rip), %rsi
    mov $message_length, %edx
    call zenith_write@PLT
    add $8, %rsp
    ret
