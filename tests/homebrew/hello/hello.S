# Hello world through the test bridge (see src/core/bridge.hpp for the contract).
    .set BRIDGE_WRITE, 0x100000     # bridgeAddress(BridgeFunction::Write)
    .set BRIDGE_EXIT,  0x100010     # bridgeAddress(BridgeFunction::Exit)

    .section .rodata
message:
    .ascii "Hello from the guest!\n"
    .set message_length, . - message

    .text
    .globl _start
_start:
    sub $8, %rsp                    # keep the stack 16-byte aligned for calls
    mov $1, %edi                    # fd 1 = stdout
    lea message(%rip), %rsi
    mov $message_length, %edx
    mov $BRIDGE_WRITE, %eax
    call *%rax
    xor %edi, %edi                  # exit code 0
    mov $BRIDGE_EXIT, %eax
    call *%rax
