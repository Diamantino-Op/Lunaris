.intel_syntax noprefix

.section .rodata
.align 8
.quad 0xf6b8f4b39de7d1ae
.quad 0xfab91a6940fcb9cf
.quad 0x785c6ed015d3e316
.quad 0x181e920a7852b9d9

.section .rodata
.align 8
.globl limine_base_revision
limine_base_revision:
    .quad 0xf9562b2d5c95a6c8
    .quad 0x6a7b384944536bdc
    .quad 6

.globl limine_framebuffer_request
limine_framebuffer_request:
    .quad 0xc7b1dd30df4c8b88
    .quad 0x0a82e883a194f07b
    .quad 0x9d5827dcd881dd75
    .quad 0xa3148604f6fab11b
    .quad 0
    .quad 0

.section .rodata
.align 8
.quad 0xadc0e0531bb10d03
.quad 0x9572709f31764c62

.section .text

.globl limine_base_revision_ptr
limine_base_revision_ptr:
    lea rax, [rip + limine_base_revision]
    ret

.globl limine_framebuffer_request_ptr
limine_framebuffer_request_ptr:
    lea rax, [rip + limine_framebuffer_request]
    ret

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
    call kernel_main
    jmp hcf
