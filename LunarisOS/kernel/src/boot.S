.intel_syntax noprefix

.section .text

.globl hlt
hlt:
    hlt
    ret

.section .text
.globl kmain
kmain:
    push rbp
    mov rbp, rsp
    and rsp, -16
    cli
    call kernel_main
    jmp hcf
