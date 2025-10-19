/**
 * HimuOperatingSystem
 *
 * File: shell.h
 * Description: Header file for UEFI shell interface
 * Module: boot
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "efi.h"

#define MAX_LINE       1024
#define MAX_ARGBUF_SIZ 2048

typedef void (*SHELL_COMMAND_FUNC)(int, void *);

typedef struct _SHELL_COMMAND
{
    const CHAR16 *CommandName;
    SHELL_COMMAND_FUNC Function;
} SHELL_COMMAND;

typedef struct
{
    uint64_t Length;
    CHAR16 String[];
} COMMAND_STRING;

/**
 * @brief Parse a command line string into a COMMAND_STRING structure.
 * @param cmdline The command line string to parse, zero-terminated.
 * @param result The resulting COMMAND_STRING structure.
 * @param bufSiz The size of the buffer for the command string.
 * @return the # of arguments parsed, or -1 on error.
 */
int ParseCommandLine(const CHAR16 *cmdline, void *buf, uint64_t bufSiz);

COMMAND_STRING *NextCommandString(COMMAND_STRING *current, void *buf, uint64_t bufSiz);

COMMAND_STRING *FindCommandString(int argc, void *buf, uint64_t bufSiz, int index);

void Shell(IN const CHAR16 *Prompt);

// Commands

void ShowMemoryMap(MAYBE_UNUSED int argc, MAYBE_UNUSED void *args);

void Boot(MAYBE_UNUSED int argc, MAYBE_UNUSED void *args);

void Dir(MAYBE_UNUSED int argc, MAYBE_UNUSED void *args);