/**
 * HIMU OPERATING SYSTEM
 *
 * File: bitmap.h
 * Kernel sturucture: Bitmap
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_KERNEL_BITMAP_H
#define __HIMUOS_KERNEL_BITMAP_H

#include "kernel/krnltypes.h"

/* Represents a bitmap structure */
struct KR_BITMAP {
    uint32_t                  Length;
    PRIVATE_DATA_MEMBER BYTE *_buffer;
};

/* Initializes a bitmap with a given buffer and length */
void KrBitMapInit(struct KR_BITMAP *btmp, BYTE *bitsBuffer, uint32_t len);

/* Checks if a specific bit in the bitmap is set. */
BOOL KrBitMapCheckBit(const struct KR_BITMAP *btmp, uint32_t idx);

/* Finds a sequence of clear (unset) bits in the bitmap.
   return -1 if failed */
int KrBitMapFindClearBits(struct KR_BITMAP *btmp, uint32_t cnt);

/* Sets or clears a specific bit in the bitmap. */
void KrBitMapSet(struct KR_BITMAP *btmp, uint32_t idx, BYTE val);

/* sets all bits in a given range of a given bitmap variable. */
void KrBitMapSetBits(struct KR_BITMAP *btmp, uint32_t startIndex, uint32_t numberToSet, BYTE val);

#define KCHECKBIT(flag, idx) ((BYTE)(1 << idx) & (flag))
#define KSETBIT(flag, idx)   ((flag) |= (1 << idx))
#define KCLEARBIT(flag, idx) ((flag) &= ~(1 << idx))

#endif //! __HIMUOS_KERNEL_BITMAP_H