;
; HIMU OPERATING SYSTEM
;
; File: osbase32.inc
; Basic Operating System Definitions for 32-bit Protected Mode

%include "boot.inc"

; -------------------------
; Selectors
; -------------------------
SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002 << 3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0