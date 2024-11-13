/**
 * HIMU OPERATING SYSTEM
 *
 * File: krnltypes.h
 * Kernel basic types
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_KERNEL_KRNLTYPES_H
#define __HIMUOS_KERNEL_KRNLTYPES_H

#include <stdint.h>

typedef uint8_t BYTE;
typedef BYTE BOOL;

#define TRUE  1
#define FALSE 0

/* This macro does nothing.
   The data members marked by this macro generally start with an underscore lowercase letter,
   which means that they are private members and should be operated with the help of other functions. */
#define PRIVATE_DATA_MEMBER

#endif // !__HIMUOS_KERNEL_KRNLTYPES_H