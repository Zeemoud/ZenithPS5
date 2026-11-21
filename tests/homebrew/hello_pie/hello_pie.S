# Hello world from a position-independent program: the pointer in .data needs a R_X86_64_RELATIVE relocation,
# applied by the loader. Built with -static-pie (see tests/homebrew/CMakeLists.txt).
    .set BRIDGE_WRITE, 0x100000
    .set BRIDGE_EXIT,  0x100010

    .section .rodata
message:
    .ascii "Hello from a relocated guest!\n"
    .set message_length, . - message

    .data
message_pointer:
    .quad message

    .text
    .globl _start
_start:
    sub $8, %rsp
    mov $1, %edi
    mov message_pointer(%rip), %rsi
    mov $message_length, %edx
    mov $BRIDGE_WRITE, %eax
    call *%rax
    xor %edi, %edi
    mov $BRIDGE_EXIT, %eax
    call *%rax
