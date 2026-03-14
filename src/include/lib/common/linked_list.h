/**
 * HimuOperatingSystem
 *
 * File: lib/common/linked_list.h
 * Description: Intrusive doubly-linked circular list.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef struct _LINKED_LIST_TAG
{
    struct _LINKED_LIST_TAG *Flink;
    struct _LINKED_LIST_TAG *Blink;
} LINKED_LIST_TAG;

HO_KERNEL_API void LinkedListInit(IN OUT LINKED_LIST_TAG *head);
HO_KERNEL_API void LinkedListInsertHead(IN OUT LINKED_LIST_TAG *head, IN OUT LINKED_LIST_TAG *entry);
HO_KERNEL_API void LinkedListInsertTail(IN OUT LINKED_LIST_TAG *head, IN OUT LINKED_LIST_TAG *entry);
HO_KERNEL_API void LinkedListRemove(IN OUT LINKED_LIST_TAG *entry);
HO_KERNEL_API BOOL LinkedListIsEmpty(IN LINKED_LIST_TAG *head);
