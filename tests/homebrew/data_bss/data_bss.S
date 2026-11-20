# Checks the loader on a real linker output with a read-only, an executable and a read/write segment:
#  - .data holds 40 and must be writable,
#  - .bss must be zero-filled.
# Returns 40 + 0 + 2 = 42.
    .data
value:
    .long 40
    .bss
    .lcomm zeroed, 4
    .text
    .globl _start
_start:
    mov value(%rip), %eax
    add zeroed(%rip), %eax
    add $2, %eax
    mov %eax, value(%rip)
    mov value(%rip), %eax
    ret
