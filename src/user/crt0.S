#include <kernel/ex/user_syscall_abi.h>

/*
 * Public compiled-userspace startup/return path:
 * - _start enters main() with a 16-byte aligned stack.
 * - Returning from main issues the formal EX_USER_SYS_EXIT contract over int 0x80.
 * - RAX carries the syscall number / return value, while RDI, RSI, and RDX
 *   carry the first three arguments.
 * - Failures return negative HO_STATUS values in RAX, but EX_USER_SYS_EXIT is a
 *   no-return termination path for the normal contract.
 */

    .section .text._start, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    andq $-16, %rsp
    call main

    movl %eax, %edi
    movl $EX_USER_SYS_EXIT, %eax
    xorl %esi, %esi
    xorl %edx, %edx
    int $EX_USER_SYSCALL_VECTOR
    ud2

    .size _start, . - _start

    .section .note.GNU-stack, "", @progbits
