bits 64
default rel

extern IdtExceptionHandler ; see src/arch/amd64/idt.c

%macro ISR_NO_ERR_STUB 1
global IsrStub%1
IsrStub%1:
    push 0
    push %1
    jmp CommonIsrStub
%endmacro

%macro ISR_ERR_STUB 1
global IsrStub%1
IsrStub%1:
    push %1
    jmp CommonIsrStub
%endmacro

CommonIsrStub:
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

    mov rdi, rsp
    call IdtExceptionHandler

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

    add rsp, 16          ; Skip error code or dummy error code
    iretq

ISR_NO_ERR_STUB 0  ; #DE Divide Error
ISR_NO_ERR_STUB 1  ; #DB Debug
ISR_NO_ERR_STUB 2  ; NMI
ISR_NO_ERR_STUB 3  ; #BP Breakpoint
ISR_NO_ERR_STUB 4  ; #OF Overflow
ISR_NO_ERR_STUB 5  ; #BR Bound Range Exceeded
ISR_NO_ERR_STUB 6  ; #UD Invalid Opcode
ISR_NO_ERR_STUB 7  ; #NM Device Not Available
ISR_ERR_STUB    8  ; #DF Double Fault
ISR_NO_ERR_STUB 9  ; Coprocessor Segment Overrun
ISR_ERR_STUB    10 ; #TS Invalid TSS
ISR_ERR_STUB    11 ; #NP Segment Not Present
ISR_ERR_STUB    12 ; #SS Stack-Segment Fault
ISR_ERR_STUB    13 ; #GP General Protection Fault
ISR_ERR_STUB    14 ; #PF Page Fault
ISR_NO_ERR_STUB 15 ; (Reserved)
ISR_NO_ERR_STUB 16 ; #MF x87 Floating-Point Exception
ISR_ERR_STUB    17 ; #AC Alignment Check
ISR_NO_ERR_STUB 18 ; #MC Machine Check
ISR_NO_ERR_STUB 19 ; #XM SIMD Floating-Point Exception
ISR_NO_ERR_STUB 20 ; #VE Virtualization Exception
ISR_NO_ERR_STUB 21 ; #CP Control Protection Exception
ISR_NO_ERR_STUB 22 ; (Reserved)
ISR_NO_ERR_STUB 23 ; (Reserved)
ISR_NO_ERR_STUB 24 ; (Reserved)
ISR_NO_ERR_STUB 25 ; (Reserved)
ISR_NO_ERR_STUB 26 ; (Reserved)
ISR_NO_ERR_STUB 27 ; (Reserved)
ISR_NO_ERR_STUB 28 ; #HV Hypervisor Injection Exception (AMD, no error code)
ISR_ERR_STUB    29 ; #VC VMM Communication Exception (AMD SEV-ES, has error code)
ISR_ERR_STUB    30 ; #SX Security Exception (has error code)
ISR_NO_ERR_STUB 31 ; (Reserved)

%assign n 32
%rep 224
ISR_NO_ERR_STUB n
%assign n n+1
%endrep

global gIsrStubTable
gIsrStubTable:
%assign n 0
%rep 256
    dq IsrStub %+ n
%assign n n+1
%endrep
