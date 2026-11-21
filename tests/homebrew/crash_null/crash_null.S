# A guest program that dereferences a null pointer. The CLI must print a crash report.
    .text
    .globl _start
_start:
    xor %eax, %eax
    mov (%rax), %eax
    ret
