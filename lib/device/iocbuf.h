/**
 * HIMU OPERATING SYSTEM
 *
 * File: iocbuf.h
 * I/O Circular Buffer
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_LIB_DEVICE_IOCBUF_H
#define __HIMUOS_LIB_DEVICE_IOCBUF_H 1

#include "krnltypes.h"
#include "kernel/task/sync.h"
#include "kernel/task/sched.h"

#define IO_CIRCULAR_BUFFER_SIZE 128

struct IO_CIR_BUFFER {
    struct KR_LOCK         Lock;
    struct KR_TASK_STRUCT *Producer;
    struct KR_TASK_STRUCT *Consumer;

    char    Buf[IO_CIRCULAR_BUFFER_SIZE];
    int32_t Head;
    int32_t Tail;
};

void IcbInit(struct IO_CIR_BUFFER *icb);

void IcbPut(struct IO_CIR_BUFFER *icb, char ch);

char IcbGet(struct IO_CIR_BUFFER *icb);

BOOL IcbFull(struct IO_CIR_BUFFER *icb);

BOOL IcbEmpty(struct IO_CIR_BUFFER *icb);

#endif