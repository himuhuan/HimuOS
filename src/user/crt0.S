#define KI_USER_BOOTSTRAP_SYSCALL_VECTOR 0x80
#define KI_USER_BOOTSTRAP_SYS_RAW_EXIT   2

    .section .text._start, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    andq $-16, %rsp
    call main

    movl %eax, %edi
    movl $KI_USER_BOOTSTRAP_SYS_RAW_EXIT, %eax
    xorl %esi, %esi
    xorl %edx, %edx
    int $KI_USER_BOOTSTRAP_SYSCALL_VECTOR
    ud2

    .size _start, . - _start

    .section .note.GNU-stack, "", @progbits
