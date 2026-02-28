; QAIOS Kernel Boot Assembly
; QKBoot.asm
; Entry point for Limine bootloader (64-bit mode)

bits 64
default rel

; ============================================================================
; Limine requests section
; ============================================================================

; Common magic for all requests
%define LIMINE_COMMON_MAGIC_0 0xc7b1dd30df4c8b88
%define LIMINE_COMMON_MAGIC_1 0x0a82e883a194f07b

section .limine_requests progbits alloc noexec write
align 8

; Base revision (required, revision 3) - only one!
global limine_base_revision
limine_base_revision:
    dq 0xf9562b2d5c95a6c8   ; Magic 1
    dq 0x6a7b384944536bdc   ; Magic 2  
    dq 3                     ; Revision 3

; Entry point request (tells Limine where to jump)
align 8
global limine_entry_point_request
limine_entry_point_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0x13d86c035a1cd3e1   ; Entry point request ID 0
    dq 0x2b0caa89d8f3026a   ; Entry point request ID 1
    dq 0                     ; Revision
    dq 0                     ; Response pointer (filled by Limine)
    dq _start                ; Entry point function

; Framebuffer request
align 8
global limine_framebuffer_request
limine_framebuffer_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0x9d5827dcd881dd75   ; Framebuffer request ID 0
    dq 0xa3148604f6fab11b   ; Framebuffer request ID 1
    dq 0                     ; Revision
    dq 0                     ; Response pointer

; Memory map request
align 8
global limine_memmap_request
limine_memmap_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0x67cf3d9d378a806f   ; Memmap request ID 0
    dq 0xe304acdfc50c3c62   ; Memmap request ID 1
    dq 0                     ; Revision
    dq 0                     ; Response pointer

; HHDM (Higher Half Direct Map) request
align 8
global limine_hhdm_request
limine_hhdm_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0x48dcf1cb8ad2b852   ; HHDM request ID 0
    dq 0x63984e959a98244b   ; HHDM request ID 1
    dq 0                     ; Revision
    dq 0                     ; Response pointer

; Kernel Address request (physical/virtual base)
align 8
global limine_kernel_address_request
limine_kernel_address_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0x71ba76863cc55f63   ; Kernel address request ID 0
    dq 0xb2644a48c516a487   ; Kernel address request ID 1
    dq 0                     ; Revision
    dq 0                     ; Response pointer

; Executable file request (kernel file buffer + cmdline)
align 8
global limine_executable_file_request
limine_executable_file_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0xad97e90e83f1ed67
    dq 0x31eb5d1c5ff23b69
    dq 0                     ; Revision
    dq 0                     ; Response pointer

; Module request (initrd / ramdisk)
align 8
global limine_module_request
limine_module_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0x3e7e279702be32af   ; Module request ID 0
    dq 0xca1c4f3bd1280cee   ; Module request ID 1
    dq 0                     ; Revision
    dq 0                     ; Response pointer
    dq 0                     ; Internal module count
    dq 0                     ; Internal modules pointer

; Terminal request
align 8
global limine_terminal_request
limine_terminal_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0xc8ac59310c2b0844
    dq 0xa68d0c7265d38878
    dq 0                     ; revision
    dq 0                     ; response pointer
    dq 0                     ; optional callback pointer

; Firmware type request (UEFI vs BIOS)
align 8
global limine_firmware_type_request
limine_firmware_type_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0x8c2f75d90bef28a8
    dq 0x7045a4688eac00c3
    dq 0                     ; revision
    dq 0                     ; response pointer

; RSDP request (ACPI entrypoint)
align 8
global limine_rsdp_request
limine_rsdp_request:
    dq LIMINE_COMMON_MAGIC_0
    dq LIMINE_COMMON_MAGIC_1
    dq 0xc5e77b6b397e7b43
    dq 0x27637845accdcf3c
    dq 0                     ; revision
    dq 0                     ; response pointer
    
; ============================================================================
; BSS section
; ============================================================================
section .bss
align 16

; Kernel stack (backup, Limine provides one)
stack_bottom:
    resb 16384  ; 16 KB stack
stack_top:

; ============================================================================
; 64-bit code section  
; ============================================================================
section .text
global _start
extern kernel_main

; Entry point - Limine enters here in 64-bit mode
_start:
    ; Disable interrupts
    cli

    ; Set up our own stack just in case
    mov rsp, stack_top
    mov rbp, rsp

    ; Clear direction flag
    cld

    ; Clear RFLAGS
    push 0
    popfq

    ; Call kernel main
    ; No parameters for now - we'll get boot info from Limine requests
    xor rdi, rdi
    xor rsi, rsi
    call kernel_main

    ; If kernel_main returns, halt
.halt:
    cli
    hlt
    jmp .halt

; Interrupt stubs
global isr_common_stub
global irq_common_stub

isr_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Call C handler
    mov rdi, rsp        ; Pass stack pointer as argument
    extern isr_handler
    call isr_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove error code and interrupt number
    add rsp, 16

    iretq

irq_common_stub:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Call C handler
    mov rdi, rsp        ; Pass stack pointer as argument
    extern irq_handler
    call irq_handler

    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    ; Remove error code and interrupt number
    add rsp, 16

    iretq

; Macro to generate ISR stubs
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0        ; Push dummy error code
    push qword %1       ; Push interrupt number
    jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    ; Error code already pushed by CPU
    push qword %1       ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro to generate IRQ stubs
%macro IRQ 2
global irq%1
irq%1:
    push qword 0        ; Push dummy error code
    push qword %2       ; Push interrupt number (vector)
    jmp irq_common_stub
%endmacro

; Exception ISRs (0-31)
ISR_NOERRCODE 0     ; Division by Zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; NMI
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Overflow
ISR_NOERRCODE 5     ; Bound Range Exceeded
ISR_NOERRCODE 6     ; Invalid Opcode
ISR_NOERRCODE 7     ; Device Not Available
ISR_ERRCODE   8     ; Double Fault
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun
ISR_ERRCODE   10    ; Invalid TSS
ISR_ERRCODE   11    ; Segment Not Present
ISR_ERRCODE   12    ; Stack Fault
ISR_ERRCODE   13    ; General Protection Fault
ISR_ERRCODE   14    ; Page Fault
ISR_NOERRCODE 15    ; Reserved
ISR_NOERRCODE 16    ; x87 FPU Error
ISR_ERRCODE   17    ; Alignment Check
ISR_NOERRCODE 18    ; Machine Check
ISR_NOERRCODE 19    ; SIMD Floating Point
ISR_NOERRCODE 20    ; Virtualization
ISR_NOERRCODE 21    ; Reserved
ISR_NOERRCODE 22    ; Reserved
ISR_NOERRCODE 23    ; Reserved
ISR_NOERRCODE 24    ; Reserved
ISR_NOERRCODE 25    ; Reserved
ISR_NOERRCODE 26    ; Reserved
ISR_NOERRCODE 27    ; Reserved
ISR_NOERRCODE 28    ; Reserved
ISR_NOERRCODE 29    ; Reserved
ISR_ERRCODE   30    ; Security Exception
ISR_NOERRCODE 31    ; Reserved

; IRQs (0-15)
IRQ 0, 32           ; Timer
IRQ 1, 33           ; Keyboard
IRQ 2, 34           ; Cascade
IRQ 3, 35           ; COM2
IRQ 4, 36           ; COM1
IRQ 5, 37           ; LPT2
IRQ 6, 38           ; Floppy
IRQ 7, 39           ; LPT1
IRQ 8, 40           ; RTC
IRQ 9, 41           ; Free
IRQ 10, 42          ; Free
IRQ 11, 43          ; Free
IRQ 12, 44          ; PS/2 Mouse
IRQ 13, 45          ; FPU
IRQ 14, 46          ; Primary ATA
IRQ 15, 47          ; Secondary ATA

; Mark stack as non-executable (silences ld executable-stack warning)
section .note.GNU-stack noalloc noexec nowrite
