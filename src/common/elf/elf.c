#include "common/elf/elf.h"
#include "arch/amd64/pm.h"

BOOL
Elf64Validate(const Elf64_Ehdr *header, uint64_t size)
{
    if (!header || size < sizeof(Elf64_Ehdr))
        return FALSE;
    const uint8_t *id = header->e_ident;
    if (id[EI_MAG0] != ELFMAG0 || id[EI_MAG1] != ELFMAG1 || id[EI_MAG2] != ELFMAG2 || id[EI_MAG3] != ELFMAG3)
        return FALSE;
    if (id[EI_CLASS] != ELFCLASS64)
        return FALSE;
    if (id[EI_DATA] != ELFDATA2LSB)
        return FALSE;
    if (id[EI_VERSION] != EV_CURRENT)
        return FALSE;
    if (header->e_version != EV_CURRENT)
        return FALSE;
    if (header->e_machine != EM_X86_64)
        return FALSE;
    if (!(header->e_type == ET_EXEC || header->e_type == ET_DYN))
        return FALSE;
    if (header->e_phnum > 0)
    {
        if (header->e_phentsize != sizeof(Elf64_Phdr))
            return FALSE;
        uint64_t end = header->e_phoff + (uint64_t)header->e_phnum * (uint64_t)header->e_phentsize;
        if (end > size || end < header->e_phoff)
            return FALSE;
    }
    return TRUE;
}

const Elf64_Ehdr *
Elf64GetHeader(const void *image, uint64_t size)
{
    const Elf64_Ehdr *header = image;
    return Elf64Validate(header, size) ? header : NULL;
}

const Elf64_Phdr *
Elf64GetProgramHeader(const void *image, uint64_t size, uint16_t *outCnt)
{
    const Elf64_Ehdr *header = Elf64GetHeader(image, size);
    if (!header || header->e_phnum == 0 || !outCnt)
        return NULL;
    const Elf64_Phdr *phdr = (const Elf64_Phdr *)((const uint8_t *)image + header->e_phoff);
    *outCnt = header->e_phnum;
    return phdr;
}

HO_NODISCARD HO_KERNEL_API uint64_t
Elf64GetSegmentPerm(uint32_t phdrFlags)
{
    uint64_t perm = 0;

    if (phdrFlags & PF_W)
        perm |= PTE_WRITABLE;
    if (!(phdrFlags & PF_X))
        perm |= PTE_NO_EXECUTE;

    // PF_R requires no extra bit on x86-64 (reads are implicit).
    return perm;
}

BOOL
Elf64GetLoadInfo(const void *image, uint64_t size, ELF64_LOAD_INFO *outInfo)
{
    if (!outInfo)
        return FALSE;
    memset(outInfo, 0, sizeof(ELF64_LOAD_INFO));

    const Elf64_Ehdr *header = Elf64GetHeader(image, size);
    if (!header)
        return FALSE;
    uint64_t entry = header->e_entry;

    uint16_t phdrCnt = 0;
    const Elf64_Phdr *phdr = Elf64GetProgramHeader(image, size, &phdrCnt);
    if (!phdr || phdrCnt == 0)
        return FALSE;

    uint64_t rxPages = 0, rwPages = 0, minAddr = ~0ull, maxAddr = 0;
    for (uint16_t i = 0; i < phdrCnt; ++i)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        uint64_t cnt = HO_ALIGN_UP(phdr[i].p_memsz, phdr[i].p_align) >> PAGE_SHIFT;
        if (phdr[i].p_flags & PF_X)
            rxPages += cnt;
        else
            rwPages += cnt;
        if (phdr[i].p_vaddr < minAddr)
            minAddr = phdr[i].p_vaddr;
        if (phdr[i].p_vaddr + phdr[i].p_memsz > maxAddr)
            maxAddr = phdr[i].p_vaddr + phdr[i].p_memsz;
    }

    outInfo->EntryVirt = entry;
    outInfo->ExecPhysPages = rxPages;
    outInfo->DataPhysPages = rwPages;
    outInfo->MaxAddrVirt = maxAddr;
    outInfo->MinAddrVirt = minAddr;
    outInfo->VirtSpanPages = (maxAddr > minAddr) ? (HO_ALIGN_UP(maxAddr - minAddr, PAGE_4KB) >> PAGE_SHIFT) : 0;
    return TRUE;
}

BOOL
Elf64Load(HO_VIRTUAL_ADDRESS minbase, void *image, uint64_t size, ELF64_MAP_SEGMENT_CALLBACK mapper)
{
    if (!mapper)
        return FALSE;
    uint16_t phdrCnt = 0;
    const Elf64_Phdr *phdr = Elf64GetProgramHeader(image, size, &phdrCnt);
    if (!phdr)
        return FALSE;

    uint16_t i;
    for (i = 0; i < phdrCnt; ++i)
    {
        const Elf64_Phdr *ph = phdr + i;
        if (ph->p_type != PT_LOAD)
            continue;
        if (minbase && ph->p_vaddr < minbase)
            continue;
        if (ph->p_filesz > 0)
        {
            uint64_t end = ph->p_offset + ph->p_filesz;
            if (end > size || end < ph->p_offset)
                return FALSE;
        }
        void *src = (uint8_t *)image + ph->p_offset;
        uint32_t perm = Elf64GetSegmentPerm(ph->p_flags);
        BOOL ret = mapper(ph->p_vaddr, src, ph->p_filesz, ph->p_memsz, perm, ph->p_align);
        if (!ret)
            return FALSE;
    }

    return TRUE;
}

HO_NODISCARD HO_KERNEL_API BOOL
Elf64LoadToBuffer(ELF64_LOAD_BUFFER_PARAMS *params)
{
    uint16_t nphdr;
    const Elf64_Phdr *phdr = Elf64GetProgramHeader(params->Image, params->ImageSize, &nphdr);
    if (!phdr || !nphdr)
        return FALSE;

    uint16_t idx;
    for (idx = 0; idx < nphdr; ++idx)
    {
        const Elf64_Phdr *ph = phdr + idx;
        if (ph->p_type != PT_LOAD)
            continue;

        // Validate segment
        if (ph->p_filesz > ph->p_memsz)
            return FALSE;
        if (ph->p_offset > params->ImageSize - ph->p_filesz)
            return FALSE;
        if (ph->p_vaddr > ~0ULL - ph->p_memsz)
            return FALSE;
        HO_VIRTUAL_ADDRESS segLow = ph->p_vaddr;
        HO_VIRTUAL_ADDRESS segHigh = ph->p_vaddr + ph->p_memsz; // no overflow here
        if (segLow < params->BaseVirt || segHigh - params->BaseVirt > params->BaseSize)
            return FALSE;

        uint64_t offset = ph->p_vaddr - params->BaseVirt;
        void *dest = (uint8_t *)params->Base + offset;
        if (ph->p_filesz > 0)
        {
            void *src = (uint8_t *)params->Image + ph->p_offset;
            memcpy(dest, src, ph->p_filesz);
        }

        // bss
        if (ph->p_memsz > ph->p_filesz)
        {
            void *bss = (uint8_t *)dest + ph->p_filesz;
            memset(bss, 0, ph->p_memsz - ph->p_filesz);
        }
    }

    return TRUE;
}
