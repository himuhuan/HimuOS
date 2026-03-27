/**
 * HimuOperatingSystem
 *
 * File: init_internal.h
 * Description: Internal declarations for kernel initialization modules.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <boot/boot_capsule.h>

HO_STATUS ImportBootMappings(STAGING_BLOCK *block);
void VerifyHhdm(STAGING_BLOCK *block);
