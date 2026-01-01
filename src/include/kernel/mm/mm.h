#pragma once

#include <arch/amd64/pm.h>
#include <kernel/hodefs.h>

#define HHDM_PHYS2VIRT(addr) ((HO_VIRTUAL_ADDRESS)((HO_PHYSICAL_ADDRESS)(addr) + HHDM_BASE_VA))