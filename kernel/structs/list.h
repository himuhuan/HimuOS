/**
 * HIMU OPERATING SYSTEM
 *
 * File: list.h
 * Kernel sturucture: double linked list
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_KERNEL_LIST_H
#define __HIMUOS_KERNEL_LIST_H

#include "../krnltypes.h"
#include <stddef.h>

#define CONTAINER_OFFSET(type, field)      (size_t)(&((type *)0)->field)

#define CONTAINER_OF(address, type, field) ((type *)((uint32_t)(address) - CONTAINER_OFFSET(type, field)))

struct KR_LIST_ELEMENT {
    struct KR_LIST_ELEMENT *Prev;
    struct KR_LIST_ELEMENT *Next;
};

struct KR_LIST {
    struct KR_LIST_ELEMENT Header;
    struct KR_LIST_ELEMENT Tail;
};

void KrInitializeList(struct KR_LIST *list);

void KrInsertElement(struct KR_LIST_ELEMENT *insertPos, struct KR_LIST_ELEMENT *element);

void KrInsertListHeader(struct KR_LIST *list, struct KR_LIST_ELEMENT *element);

void KrInsertListTail(struct KR_LIST *list, struct KR_LIST_ELEMENT *element);

void KrRemoveElement(struct KR_LIST_ELEMENT *element);

struct KR_LIST_ELEMENT *KrListPopHeader(struct KR_LIST *list);

BOOL KrListHasElement(struct KR_LIST *list, struct KR_LIST_ELEMENT *element);

BOOL KrListIsEmpty(struct KR_LIST *list);

size_t KrListLength(struct KR_LIST *list);

#endif //! __HIMUOS_KERNEL_LIST_H