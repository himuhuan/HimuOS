/**
 * HimuOperatingSystem
 *
 * File: user/input_probe/main.c
 * Description: Bounded foreground handoff probe for the user_input profile.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gInputProbePrompt[] = "input_probe> ";
static const char gInputProbePrefix[] = "[INPUTPROBE] ";
static const char gInputProbeHandoff[] = "[INPUTPROBE] handoff\n";
static const char gNewLine[] = "\n";

static uint64_t
HoInputProbeStringLength(const char *value)
{
    uint64_t length = 0;

    while (value[length] != '\0')
        ++length;

    return length;
}

static void
HoInputProbeMustWrite(const char *buffer, uint64_t length)
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
HoInputProbeMustWriteLiteral(const char *literal)
{
    HoInputProbeMustWrite(literal, HoInputProbeStringLength(literal));
}

static void
HoInputProbeAppendLiteral(char *buffer, uint64_t *offset, uint64_t capacity, const char *literal)
{
    uint64_t literalLength = HoInputProbeStringLength(literal);

    if ((*offset + literalLength) > capacity)
        HoUserAbort();

    for (uint64_t index = 0; index < literalLength; ++index)
    {
        buffer[*offset] = literal[index];
        *offset += 1U;
    }
}

static void
HoInputProbeAppendBytes(char *buffer, uint64_t *offset, uint64_t capacity, const char *value, uint64_t valueLength)
{
    if ((*offset + valueLength) > capacity)
        HoUserAbort();

    for (uint64_t index = 0; index < valueLength; ++index)
    {
        buffer[*offset] = value[index];
        *offset += 1U;
    }
}

static void
HoInputProbeWriteLine(const char *buffer, uint64_t length)
{
    char output[(sizeof(gInputProbePrefix) - 1U) + EX_USER_READLINE_MAX_LENGTH + (sizeof(gNewLine) - 1U)];
    uint64_t outputLength = 0;

    HoInputProbeAppendLiteral(output, &outputLength, sizeof(output), gInputProbePrefix);
    HoInputProbeAppendBytes(output, &outputLength, sizeof(output), buffer, length);
    HoInputProbeAppendLiteral(output, &outputLength, sizeof(output), gNewLine);
    HoInputProbeMustWrite(output, outputLength);
}

int
main(void)
{
    char line[EX_USER_READLINE_MAX_LENGTH];
    int64_t status = 0;

    if (!HoUserCurrentCapabilitySeedBlockIsValid())
        HoUserAbort();

    HoInputProbeMustWriteLiteral(gInputProbePrompt);
    status = HoUserReadLine(line, sizeof(line));
    if (status < 0)
        HoUserAbort();

    HoInputProbeWriteLine(line, (uint64_t)status);

    status = HoUserReadLine(line, sizeof(line));
    if (status != -(int64_t)EC_INVALID_STATE)
        HoUserAbort();

    HoInputProbeMustWriteLiteral(gInputProbeHandoff);
    HoUserExit(0);
}
