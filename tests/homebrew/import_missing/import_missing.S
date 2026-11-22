# Calls an import that the host does not implement: the program must stop with a report naming the function.
    .text
    .globl _start
_start:
    sub $8, %rsp
    call zenith_unimplemented_demo@PLT
    add $8, %rsp
    ret
