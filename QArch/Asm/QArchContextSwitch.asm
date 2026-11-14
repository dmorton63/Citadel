bits 64
default rel

section .text

global QArchSwitchTaskContext

QArchSwitchTaskContext:
    test rdi, rdi
    jz .initial

    mov [rdi + 136], rdx
    fxsave64 [rdi + 176]
    mov [rdi + 0], rax
    mov [rdi + 8], rbx
    mov [rdi + 16], rcx
    mov [rdi + 24], rdx
    mov [rdi + 32], rsi
    mov [rdi + 40], rdi
    mov [rdi + 48], rbp
    mov [rdi + 56], rsp
    mov [rdi + 64], r8
    mov [rdi + 72], r9
    mov [rdi + 80], r10
    mov [rdi + 88], r11
    mov [rdi + 96], r12
    mov [rdi + 104], r13
    mov [rdi + 112], r14
    mov [rdi + 120], r15
    lea rax, [rel .resume]
    mov [rdi + 128], rax
    xor eax, eax
    mov ax, cs
    mov [rdi + 144], rax
    mov ax, ss
    mov [rdi + 152], rax

    jmp .restore

.initial:
    mov qword [rsi + 136], 0x202

.restore:
    mov r10, rsi
    mov rsp, [r10 + 56]
    fxrstor64 [r10 + 176]
    push qword [r10 + 128]
    test qword [r10 + 136], 0x200
    jz .restore_interrupts_disabled
    mov rax, [r10 + 0]
    mov rbx, [r10 + 8]
    mov rcx, [r10 + 16]
    mov rdx, [r10 + 24]
    mov rsi, [r10 + 32]
    mov rdi, [r10 + 40]
    mov rbp, [r10 + 48]
    mov r8, [r10 + 64]
    mov r9, [r10 + 72]
    mov r11, [r10 + 88]
    mov r12, [r10 + 96]
    mov r13, [r10 + 104]
    mov r14, [r10 + 112]
    mov r15, [r10 + 120]
    mov r10, [r10 + 80]
    sti
    ret

.restore_interrupts_disabled:
    mov rax, [r10 + 0]
    mov rbx, [r10 + 8]
    mov rcx, [r10 + 16]
    mov rdx, [r10 + 24]
    mov rsi, [r10 + 32]
    mov rdi, [r10 + 40]
    mov rbp, [r10 + 48]
    mov r8, [r10 + 64]
    mov r9, [r10 + 72]
    mov r11, [r10 + 88]
    mov r12, [r10 + 96]
    mov r13, [r10 + 104]
    mov r14, [r10 + 112]
    mov r15, [r10 + 120]
    mov r10, [r10 + 80]
    ret

.resume:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
