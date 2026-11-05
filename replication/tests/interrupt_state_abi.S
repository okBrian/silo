    .macro snapshot destination
    .set slot, 0
    .irp reg, rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8, r9, r10, r11, r12, r13, r14, r15
    movq %\reg, \destination+slot(%rip)
    .set slot, slot + 8
    .endr
    pushfq
    popq \destination+128(%rip)
    movq -16(%rsp), %rax
    movq %rax, \destination+136(%rip)
    movq \destination(%rip), %rax
    fxsave64 \destination+144(%rip)
    .endm

    .text
    .globl interrupt_state_round
    .type interrupt_state_round, @function
interrupt_state_round:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    fxsave64 original_fp(%rip)
    fninit
    fldcw test_x87_control(%rip)
    fld1
    fldpi
    ldmxcsr test_mxcsr(%rip)
    .set slot, 0
    .irp i, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    movdqa xmm_seeds+slot(%rip), %xmm\i
    .set slot, slot + 16
    .endr

    movabsq $0x123456789abcdef0, %rax
    movq %rax, -16(%rsp)
    .set seed, 0x123456789abcdef1
    .irp reg, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10, r11, r12, r13, r14, r15
    movabsq $seed, %\reg
    .set seed, seed + 1
    .endr
    cmpb $1, %al
    stc
    snapshot interrupt_state_before
    int3
    snapshot interrupt_state_after

    fxrstor64 original_fp(%rip)
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
    .size interrupt_state_round, .-interrupt_state_round

    .globl clobber_interrupt_state
    .type clobber_interrupt_state, @function
clobber_interrupt_state:
    fninit
    ldmxcsr handler_mxcsr(%rip)
    pxor %xmm0, %xmm0
    pxor %xmm15, %xmm15
    xorl %eax, %eax
    ret
    .size clobber_interrupt_state, .-clobber_interrupt_state

    .section .rodata
    .p2align 4
xmm_seeds:
    .set seed, 0
    .rept 16
    .quad 0x123456789abcdef0 + seed, 0x0fedcba987654321 + seed
    .set seed, seed + 1
    .endr
test_x87_control:
    .short 0x077f
    .p2align 2
test_mxcsr:
    .long 0x3f80
handler_mxcsr:
    .long 0x1f80

    .bss
    .p2align 4
original_fp:
    .zero 512
    .section .note.GNU-stack,"",@progbits
