# Smallest possible guest program: returns 42 to the host.
    .text
    .globl _start
_start:
    mov $42, %eax
    ret
