/**
 * HimuOperatingSystem
 *
 * File: blmm_manifest.c
 * Description: Boot mapping manifest operations — append, sort, validate.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

BOOT_MAPPING_MANIFEST_ENTRY *
BlGetBootMappingManifestEntries(BOOT_CAPSULE *capsule)
{
    if (capsule == NULL || capsule->MappingManifest.EntriesPhys == 0)
        return NULL;
    return (BOOT_MAPPING_MANIFEST_ENTRY *)(UINTN)capsule->MappingManifest.EntriesPhys;
}

void
BlCopyBootMappingManifestEntry(BOOT_MAPPING_MANIFEST_ENTRY *dst, const BOOT_MAPPING_MANIFEST_ENTRY *src)
{
    if (dst == NULL || src == NULL)
        return;

    dst->Category = src->Category;
    dst->CacheType = src->CacheType;
    dst->Attributes = src->Attributes;
    dst->VirtStart = src->VirtStart;
    dst->VirtSize = src->VirtSize;
    dst->PhysStart = src->PhysStart;
    dst->PhysSize = src->PhysSize;
    dst->PageSize = src->PageSize;
    dst->RawPteFlags = src->RawPteFlags;
}

UINT32
BlGetBootMappingCacheType(UINT64 flags)
{
    if (flags & PTE_CACHE_DISABLE)
        return BOOT_MAPPING_CACHE_UNCACHEABLE;
    if (flags & PTE_WRITETHROUGH)
        return BOOT_MAPPING_CACHE_WRITE_THROUGH;
    return BOOT_MAPPING_CACHE_DEFAULT;
}

UINT64
BlGetBootMappingEntryAttributes(UINT64 flags, UINT64 extraAttributes)
{
    UINT64 attrs = BOOT_MAPPING_ATTR_PRESENT | BOOT_MAPPING_ATTR_READ | extraAttributes;

    if (flags & PTE_WRITABLE)
        attrs |= BOOT_MAPPING_ATTR_WRITE;
    if ((flags & PTE_NO_EXECUTE) == 0)
        attrs |= BOOT_MAPPING_ATTR_EXECUTE;
    if (flags & PTE_USER)
        attrs |= BOOT_MAPPING_ATTR_USER;

    return attrs;
}

BOOL
BlIsBootMappingUmbrellaCategory(UINT32 category)
{
    return category == BOOT_MAPPING_CATEGORY_BOOTSTRAP_IDENTITY || category == BOOT_MAPPING_CATEGORY_HHDM;
}

EFI_STATUS
BlAppendBootMappingEntry(BOOT_CAPSULE *capsule, const BOOT_MAPPING_MANIFEST_ENTRY *entry)
{
    if (capsule == NULL || entry == NULL)
        return EFI_INVALID_PARAMETER;

    if (!BootMappingManifestHasValidLayout(capsule->BasePhys, sizeof(BOOT_CAPSULE), capsule->Layout.HeaderSize,
                                           &capsule->MappingManifest))
    {
        LOG_ERROR("Boot mapping manifest header is invalid before append\r\n");
        return EFI_INVALID_PARAMETER;
    }

    if (entry->Category == BOOT_MAPPING_CATEGORY_NONE)
    {
        LOG_ERROR("Boot mapping manifest entry missing semantic category\r\n");
        return EFI_INVALID_PARAMETER;
    }

    if (entry->VirtSize == 0 || entry->PhysSize == 0 || entry->PageSize == 0)
    {
        LOG_ERROR("Boot mapping manifest entry has empty size fields\r\n");
        return EFI_INVALID_PARAMETER;
    }

    BOOT_MAPPING_MANIFEST *manifest = &capsule->MappingManifest;
    BOOT_MAPPING_MANIFEST_ENTRY *entries = BlGetBootMappingManifestEntries(capsule);
    if (entries == NULL)
        return EFI_INVALID_PARAMETER;

    for (UINT32 idx = 0; idx < manifest->EntryCount; ++idx)
    {
        BOOT_MAPPING_MANIFEST_ENTRY *existing = &entries[idx];
        UINT64 entryVirtEnd = entry->VirtStart + entry->VirtSize;
        UINT64 existingVirtEnd = existing->VirtStart + existing->VirtSize;
        BOOL overlapsVirt = !(entryVirtEnd <= existing->VirtStart || existingVirtEnd <= entry->VirtStart);

        if (!overlapsVirt)
            continue;

        if (existing->Category == entry->Category)
        {
            LOG_ERROR("Boot mapping manifest overlap in category=%u at VA=[%p,%p)\r\n", entry->Category,
                      (void *)(UINTN)entry->VirtStart, (void *)(UINTN)entryVirtEnd);
            return EFI_INVALID_PARAMETER;
        }

        if (!BlIsBootMappingUmbrellaCategory(existing->Category) && !BlIsBootMappingUmbrellaCategory(entry->Category))
        {
            LOG_ERROR("Boot mapping manifest conflicting ownership categories=%u/%u at VA=%p\r\n", existing->Category,
                      entry->Category, (void *)(UINTN)entry->VirtStart);
            return EFI_INVALID_PARAMETER;
        }
    }

    if (manifest->EntryCount >= manifest->EntryCapacity)
    {
        LOG_ERROR("Boot mapping manifest overflow: count=%u capacity=%u\r\n", manifest->EntryCount,
                  manifest->EntryCapacity);
        return EFI_OUT_OF_RESOURCES;
    }

    BlCopyBootMappingManifestEntry(&entries[manifest->EntryCount], entry);
    manifest->EntryCount++;
    return EFI_SUCCESS;
}

void
BlSortBootMappingManifest(BOOT_CAPSULE *capsule)
{
    BOOT_MAPPING_MANIFEST_ENTRY *entries = BlGetBootMappingManifestEntries(capsule);
    UINT32 count = capsule->MappingManifest.EntryCount;

    if (entries == NULL || count < 2)
        return;

    for (UINT32 idx = 1; idx < count; ++idx)
    {
        BOOT_MAPPING_MANIFEST_ENTRY key;
        BlCopyBootMappingManifestEntry(&key, &entries[idx]);
        UINT32 pos = idx;

        while (pos > 0 && BootMappingManifestCompareEntryOrder(&entries[pos - 1], &key) > 0)
        {
            BlCopyBootMappingManifestEntry(&entries[pos], &entries[pos - 1]);
            pos--;
        }

        BlCopyBootMappingManifestEntry(&entries[pos], &key);
    }
}

EFI_STATUS
BlValidateBootMappingManifest(BOOT_CAPSULE *capsule)
{
    if (capsule == NULL)
        return EFI_INVALID_PARAMETER;

    BOOT_MAPPING_MANIFEST *manifest = &capsule->MappingManifest;
    BOOT_MAPPING_MANIFEST_ENTRY *entries = BlGetBootMappingManifestEntries(capsule);
    UINT64 seenCategories = 0;

    if (!BootMappingManifestHasValidLayout(capsule->BasePhys, sizeof(BOOT_CAPSULE), capsule->Layout.HeaderSize,
                                           manifest))
    {
        LOG_ERROR("Boot mapping manifest layout validation failed\r\n");
        return EFI_INVALID_PARAMETER;
    }

    if (entries == NULL)
        return EFI_INVALID_PARAMETER;

    for (UINT32 idx = 0; idx < manifest->EntryCount; ++idx)
    {
        BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[idx];
        if ((entry->Attributes & BOOT_MAPPING_ATTR_BOOT_IMPORTED) == 0)
        {
            LOG_ERROR("Boot mapping manifest entry %u missing BOOT_IMPORTED attribute\r\n", idx);
            return EFI_INVALID_PARAMETER;
        }

        if (idx > 0 && !BootMappingManifestEntryIsOrdered(&entries[idx - 1], entry))
        {
            LOG_ERROR("Boot mapping manifest entry order is not deterministic at index=%u\r\n", idx);
            return EFI_INVALID_PARAMETER;
        }

        seenCategories |= BootMappingCategoryMask(entry->Category);
    }

    if ((manifest->RequiredCategories & seenCategories) != manifest->RequiredCategories)
    {
        LOG_ERROR("Boot mapping manifest missing required categories: required=0x%x seen=0x%x\r\n",
                  manifest->RequiredCategories, seenCategories);
        return EFI_INVALID_PARAMETER;
    }

    return EFI_SUCCESS;
}
