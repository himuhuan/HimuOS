/**
 * HIMU OPERATING SYSTEM
 *
 * File: bitmap.c
 * Kernel sturucture: Bitmap
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */
#include "bitmap.h"
#include "kernel/krnldbg.h"
#include <stddef.h>
#include <string.h>

void KrBitMapInit(struct KR_BITMAP *btmp, BYTE *bitsBuffer, uint32_t len) {
    btmp->Length  = len;
    btmp->_buffer = bitsBuffer;
    memset(btmp->_buffer, 0, len);
}

BOOL KrBitMapCheckBit(const struct KR_BITMAP *btmp, uint32_t idx) {
    KASSERT(btmp->_buffer != NULL && btmp->Length > 0);
    uint32_t bufferIdx, bitIdx;

    bufferIdx = idx / 8;
    bitIdx    = idx % 8;
    return (btmp->_buffer[bufferIdx] & (1 << bitIdx));
}

int KrBitMapFindClearBits(struct KR_BITMAP *btmp, uint32_t cnt) {
    uint32_t idxByte;
    int      idxBitInByte, idxBitInBitmap, idxFound;
    int      bitsLeft, cntBitFound;

    KASSERT(btmp != NULL && cnt > 0);
    idxByte = 0;
    while (btmp->_buffer[idxByte] == 0xff && idxByte < btmp->Length)
        ++idxByte;
    if (idxByte == btmp->Length)
        return -1;

    idxBitInByte = 0;
    while (KCHECKBIT(btmp->_buffer[idxByte], idxBitInByte))
        idxBitInByte++;

    idxBitInBitmap = idxByte * 8 + idxBitInByte;
    if (cnt == 1)
        return idxBitInBitmap;

    bitsLeft    = btmp->Length * 8 - idxBitInBitmap;
    idxFound    = -1;
    cntBitFound = 1;
    idxBitInBitmap++;
    while (bitsLeft-- > 0) {
        if (!KrBitMapCheckBit(btmp, idxBitInBitmap))
            ++cntBitFound;
        else
            cntBitFound = 0;

        if (cntBitFound == cnt) {
            idxFound = idxBitInBitmap - cnt + 1;
            break;
        }
        idxBitInBitmap++;
    }
    return idxFound;
}

void KrBitMapSet(struct KR_BITMAP *btmp, uint32_t idx, BYTE val) {
    KASSERT(val == 1 || val == 0);
    int idxByte, idxBitInByte;

    idxByte      = idx / 8;
    idxBitInByte = idx % 8;
    if (val) {
        KSETBIT(btmp->_buffer[idxByte], idxBitInByte);
    } else {
        KCLEARBIT(btmp->_buffer[idxByte], idxBitInByte);
    }
}

void KrBitMapSetBits(struct KR_BITMAP *btmp, uint32_t startIndex, uint32_t numberToSet, BYTE val) {
    KASSERT(val == 1 || val == 0);
    int idxByte, idxBitInByte, cnt, idx;

    for (cnt = 0; cnt < numberToSet; ++cnt) {
        idx          = startIndex + cnt;
        idxByte      = idx / 8;
        idxBitInByte = idx % 8;
        if (val) {
            KSETBIT(btmp->_buffer[idxByte], idxBitInByte);
        } else {
            KCLEARBIT(btmp->_buffer[idxByte], idxBitInByte);
        }
    }
}