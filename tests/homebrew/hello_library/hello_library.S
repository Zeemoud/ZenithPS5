# A program that calls a library (libzenithgreet.so), which itself calls the host (zenith_write):
# program -> library -> host. Then the program exits.
    .text
    .globl _start
_start:
    sub $8, %rsp
    call zenith_greet@PLT
    xor %edi, %edi
    call zenith_exit@PLT
