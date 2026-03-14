/**
 * HimuOperatingSystem
 *
 * File: lib/common/linked_list.c
 * Description: Intrusive doubly-linked circular list implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <lib/common/linked_list.h>

void LinkedListInit(IN OUT LINKED_LIST_TAG *head)
{
    head->Flink = head;
    head->Blink = head;
}

void LinkedListInsertHead(IN OUT LINKED_LIST_TAG *head, IN OUT LINKED_LIST_TAG *entry)
{
    entry->Flink = head->Flink;
    entry->Blink = head;
    head->Flink->Blink = entry;
    head->Flink = entry;
}

void LinkedListInsertTail(IN OUT LINKED_LIST_TAG *head, IN OUT LINKED_LIST_TAG *entry)
{
    entry->Flink = head;
    entry->Blink = head->Blink;
    head->Blink->Flink = entry;
    head->Blink = entry;
}

void LinkedListRemove(IN OUT LINKED_LIST_TAG *entry)
{
    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;
    entry->Flink = entry;
    entry->Blink = entry;
}

BOOL LinkedListIsEmpty(IN LINKED_LIST_TAG *head)
{
    return head->Flink == head;
}
