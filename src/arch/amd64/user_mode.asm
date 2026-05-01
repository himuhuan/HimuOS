;
; HimuOperatingSystem
;
; File: arch/amd64/user_mode.asm
; Description:
; Minimal first-entry helper that builds an iretq frame on the current kernel
; stack and enters the staged user-mode context.
;
; Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
;

bits 64
default rel

section .text

; void KiUserModeIretq(uint64_t userRip,
;                           uint64_t userRsp,
;                           uint64_t userRflags,
;                           uint64_t userCs,
;                           uint64_t userSs);
;
; System V AMD64 calling convention:
;   rdi = userRip
;   rsi = userRsp
;   rdx = userRflags
;   rcx = userCs
;   r8  = userSs

global KiUserModeIretq
KiUserModeIretq:
    mov ax, r8w
    mov ds, ax
    mov es, ax

    push r8
    push rsi
    push rdx
    push rcx
    push rdi

    cld

    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d

    iretq
