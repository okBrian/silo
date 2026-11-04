    .text
    .globl checked_context_entry
    .type checked_context_entry, @function
checked_context_entry:
    # SysV function entry has (%rsp + 8) aligned to 16 bytes.
    movq %rsp, %rax
    andl $15, %eax
    cmpl $8, %eax
    jne .Lbad_alignment
    jmp context_test_entry
.Lbad_alignment:
    ud2
    .size checked_context_entry, .-checked_context_entry

    .globl check_preserved_registers
    .type check_preserved_registers, @function
check_preserved_registers:
    # The third argument is a different seed on each stack. Keep a copy on
    # this stack and load distinct values into all six nonvolatile GPRs.
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    pushq %rdx
    movq %rdx, %rbx
    leaq 1(%rdx), %rbp
    leaq 2(%rdx), %r12
    leaq 3(%rdx), %r13
    leaq 4(%rdx), %r14
    leaq 5(%rdx), %r15
    movq %rdi, %rax
    movq %rsi, %rdi
    call *%rax

    xorl %eax, %eax
    popq %rdx
    cmpq %rdx, %rbx
    jne .Lrestore
    incq %rdx
    cmpq %rdx, %rbp
    jne .Lrestore
    incq %rdx
    cmpq %rdx, %r12
    jne .Lrestore
    incq %rdx
    cmpq %rdx, %r13
    jne .Lrestore
    incq %rdx
    cmpq %rdx, %r14
    jne .Lrestore
    incq %rdx
    cmpq %rdx, %r15
    sete %al
.Lrestore:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
    .size check_preserved_registers, .-check_preserved_registers

    .section .note.GNU-stack,"",@progbits
