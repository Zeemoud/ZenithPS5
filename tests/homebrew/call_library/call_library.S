# A program that calls a function of a shared library (libzenithmath.so) and exits with the result:
# zenith_add(40, 2) = 42. ZenithPS5 finds the library as a file next to the program.
    .text
    .globl _start
_start:
    sub $8, %rsp
    mov $40, %edi
    mov $2, %esi
    call zenith_add@PLT
    mov %eax, %edi
    call zenith_exit@PLT
