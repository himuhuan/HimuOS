bits 64
default rel

extern IdtDispatchInterrupt ; see src/arch/amd64/idt.c

%macro ISR_NO_ERR_STUB 1
global IsrStub%1
IsrStub%1:
    push qword 0
    push qword %1
    jmp CommonIsrStub
%endmacro

%macro ISR_ERR_STUB 1
global IsrStub%1
IsrStub%1:
    push qword %1
    jmp CommonIsrStub
%endmacro

%macro GEN_ISR_STUB 1
%if %1 = 8 || %1 = 10 || %1 = 11 || %1 = 12 || %1 = 13 || %1 = 14 || %1 = 17 || %1 = 21 || %1 = 29 || %1 = 30
    ISR_ERR_STUB %1
%else
    ISR_NO_ERR_STUB %1
%endif
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
    call IdtDispatchInterrupt

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

    add rsp, 16 ; Skip vector number and error code/dummy error code
    iretq

%assign i 0
%rep 256
    GEN_ISR_STUB i
%assign i i + 1
%endrep

global gIsrStubTable
gIsrStubTable:
%assign i 0
%rep 256
    dq IsrStub %+ i
%assign i i + 1
%endrep
