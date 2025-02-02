#! /bin/bash

# nasm -I include/ -f elf -o build/krnlio_asm.o lib/kernel/krnlio.asm \
# && nasm -I include/ -f elf -o build/kernel_asm.o ./kernel/kernel.S \
# && gcc -I lib -I . -fno-stack-protector -fno-builtin -c -m32 -o build/clock_irq.o ./kernel/i8253.c \
# && gcc -I lib -I . -fno-stack-protector -fno-builtin -c -m32 -o build/kernel.o ./kernel/kernel.c \
# && gcc -I lib -I . -fno-stack-protector -fno-builtin -c -m32 -o build/interrupt.o ./kernel/interrupt.c \
# && gcc -I lib -I . -fno-stack-protector -fno-builtin -c -m32 -o build/init.o ./kernel/init.c \
# && ld -melf_i386 -Ttext 0xc0100000 -e KrnlEntry -o build/kernel.bin build/kernel.o build/clock_irq.o build/krnlio_asm.o build/interrupt.o build/init.o build/kernel_asm.o \
# && 
nasm -I lib/asm/include/ -o build/mbr.bin boot/mbr.asm \
&& nasm -I lib/asm/include/ -o build/loader.bin boot/loader.asm \
&& dd if=build/mbr.bin of=disk.img bs=512 count=1 conv=notrunc \
&& dd if=build/loader.bin of=disk.img bs=512 count=4 seek=2 conv=notrunc

