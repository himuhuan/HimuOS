;
; HimuOperatingSystem
;
; File: arch/amd64/context_switch.asm
; Description:
; Assembly context switch primitive for kernel threads.
; Saves/restores callee-saved registers per System V AMD64 ABI.
;
; Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
;

; KTHREAD_CONTEXT field offsets (must match C struct layout)
CTX_RBX equ 0
CTX_RBP equ 8
CTX_R12 equ 16
CTX_R13 equ 24
CTX_R14 equ 32
CTX_R15 equ 40
CTX_RSP equ 48
; CTX_RIP equ 56  (informational, not used here — RIP is on stack via ret)

section .text

; void KiSwitchContext(KTHREAD_CONTEXT *prev, KTHREAD_CONTEXT *next)
;
; System V AMD64 calling convention:
;   rdi = prev (KTHREAD_CONTEXT of outgoing thread)
;   rsi = next (KTHREAD_CONTEXT of incoming thread)
;
; Called with interrupts disabled (IF=0).  The caller is responsible
; for disabling interrupts before the call.  The target thread restores
; its own interrupt state upon return.
;
; For a running thread being switched out, [rsp] holds the return
; address pushed by the CALL instruction.  Saving RSP therefore
; captures the implicit RIP.  Restoring RSP and executing RET
; resumes at the saved return address.
;
; For a NEW thread, KeThreadCreate sets up:
;   [stack_top - 8]  = 0                    (dummy return addr / alignment)
;   [stack_top - 16] = &KiThreadTrampoline  (popped by RET)
;   Context.RSP      = stack_top - 16
;
global KiSwitchContext
KiSwitchContext:
    ; ── save prev ──
    mov [rdi + CTX_RBX], rbx
    mov [rdi + CTX_RBP], rbp
    mov [rdi + CTX_R12], r12
    mov [rdi + CTX_R13], r13
    mov [rdi + CTX_R14], r14
    mov [rdi + CTX_R15], r15
    mov [rdi + CTX_RSP], rsp

    ; ── restore next ──
    mov rbx, [rsi + CTX_RBX]
    mov rbp, [rsi + CTX_RBP]
    mov r12, [rsi + CTX_R12]
    mov r13, [rsi + CTX_R13]
    mov r14, [rsi + CTX_R14]
    mov r15, [rsi + CTX_R15]
    mov rsp, [rsi + CTX_RSP]

    ret
