/**
 * HimuOperatingSystem
 *
 * File: user/line_echo/main.c
 * Description: Bounded foreground line echo payload for input handoff tests.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gLineEchoPrompt[] = "line_echo> ";
static const char gLineEchoPrefix[] = "[LINEECHO] ";
static const char gNewLine[] = "\n";

static uint64_t
HoLineEchoStringLength(const char *value)
{
    uint64_t length = 0;

    while (value[length] != '\0')
        ++length;

    return length;
}

static void
HoLineEchoMustWrite(const char *buffer, uint64_t length)
{
    while (length != 0U)
    {
        uint64_t chunkLength = length;
        if (chunkLength > EX_USER_SYSCALL_WRITE_MAX_LENGTH)
            chunkLength = EX_USER_SYSCALL_WRITE_MAX_LENGTH;

        if (HoUserWriteStdout(buffer, chunkLength) != (int64_t)chunkLength)
            HoUserAbort();

        buffer += chunkLength;
        length -= chunkLength;
    }
}

static void
HoLineEchoMustWriteLiteral(const char *literal)
{
    HoLineEchoMustWrite(literal, HoLineEchoStringLength(literal));
}

static void
HoLineEchoAppendLiteral(char *buffer, uint64_t *offset, uint64_t capacity, const char *literal)
{
    uint64_t literalLength = HoLineEchoStringLength(literal);

    if ((*offset + literalLength) > capacity)
        HoUserAbort();

    for (uint64_t index = 0; index < literalLength; ++index)
    {
        buffer[*offset] = literal[index];
        *offset += 1U;
    }
}

static void
HoLineEchoAppendBytes(char *buffer, uint64_t *offset, uint64_t capacity, const char *value, uint64_t valueLength)
{
    if ((*offset + valueLength) > capacity)
        HoUserAbort();

    for (uint64_t index = 0; index < valueLength; ++index)
    {
        buffer[*offset] = value[index];
        *offset += 1U;
    }
}

static int64_t
HoLineEchoReadLineBlocking(char *buffer, uint64_t capacity)
{
    for (;;)
    {
        int64_t status = HoUserReadLine(buffer, capacity);
        if (status >= 0)
            return status;

        if (status != -(int64_t)EC_INVALID_STATE)
            HoUserAbort();

        (void)HoUserSleepMs(10U);
    }
}

int
main(void)
{
    char line[EX_USER_READLINE_MAX_LENGTH];
    char output[(sizeof(gLineEchoPrefix) - 1U) + EX_USER_READLINE_MAX_LENGTH + (sizeof(gNewLine) - 1U)];
    uint64_t outputLength = 0;
    int64_t status = 0;

    if (!HoUserCurrentCapabilitySeedBlockIsValid())
        HoUserAbort();

    HoLineEchoMustWriteLiteral(gLineEchoPrompt);
    status = HoLineEchoReadLineBlocking(line, sizeof(line));

    HoLineEchoAppendLiteral(output, &outputLength, sizeof(output), gLineEchoPrefix);
    HoLineEchoAppendBytes(output, &outputLength, sizeof(output), line, (uint64_t)status);
    HoLineEchoAppendLiteral(output, &outputLength, sizeof(output), gNewLine);
    HoLineEchoMustWrite(output, outputLength);

    HoUserExit(0);
}
