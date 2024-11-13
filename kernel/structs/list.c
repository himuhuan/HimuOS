/**
 * HIMU OPERATING SYSTEM
 *
 * File: list.c
 * Kernel sturucture: double linked list
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "../interrupt.h"
#include "list.h"

void KrInitializeList(struct KR_LIST *list) {
    list->Header.Prev = NULL;
    list->Header.Next = &list->Tail;
    list->Tail.Prev   = &list->Header;
    list->Tail.Next   = NULL;
}

void KrInsertElement(struct KR_LIST_ELEMENT *insertPos, struct KR_LIST_ELEMENT *element) {
    uint8_t status        = DisableIntr();
    insertPos->Prev->Next = element;
    element->Prev         = insertPos->Prev;
    element->Next         = insertPos;
    insertPos->Prev       = element;
    SetIntrStatus(status);
}

void KrInsertListHeader(struct KR_LIST *list, struct KR_LIST_ELEMENT *element) {
    KrInsertElement(list->Header.Next, element);
}

void KrInsertListTail(struct KR_LIST *list, struct KR_LIST_ELEMENT *element) { KrInsertElement(&list->Tail, element); }

void KrRemoveElement(struct KR_LIST_ELEMENT *element) {
    uint8_t status      = DisableIntr();
    element->Prev->Next = element->Next;
    element->Next->Prev = element->Prev;
    SetIntrStatus(status);
}

struct KR_LIST_ELEMENT *KrListPopHeader(struct KR_LIST *list) {
    struct KR_LIST_ELEMENT *element = list->Header.Next;
    KrRemoveElement(element);
    return element;
}

BOOL KrListHasElement(struct KR_LIST *list, struct KR_LIST_ELEMENT *target) {
    struct KR_LIST_ELEMENT *element;
    for (element = list->Header.Next; element != &list->Tail; element = element->Next) {
        if (element == target)
            return TRUE;
    }
    return FALSE;
}

BOOL KrListIsEmpty(struct KR_LIST *list) { return list->Header.Next == &list->Tail; }

size_t KrListLength(struct KR_LIST *list) {
    size_t                  len;
    struct KR_LIST_ELEMENT *p;

    len = 0;
    for (p = list->Header.Next; p != &list->Tail; p = p->Next)
        ++len;

    return len;
}
