/**
 * HimuOperatingSystem
 *
 * File: user/hsh/main.c
 * Description: P1 skeleton by default, or the minimal P2 demo-shell REPL when
 *              built for the demo_shell profile.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gHshPrompt[] = "hsh> ";

#if defined(HO_DEMO_TEST_DEMO_SHELL)

enum
{
    HO_HSH_MAX_JOBS = 4U,
};

typedef struct HO_HSH_JOB
{
    uint32_t Pid;
    const char *Name;
    BOOL Background;
    BOOL Alive;
} HO_HSH_JOB;

static const char gHelpText[] = "help\nexit\ncalc\n& tick1s\n";
static const char gTick1sName[] = "tick1s";
static const char gStartedPrefix[] = "[HSH] started pid=";
static const char gStartedNamePrefix[] = " name=";
static const char gStartedBackgroundPrefix[] = " bg=";
static const char gUnknownCommand[] = "[HSH] unknown command\n";
static const char gSpawnFailed[] = "[HSH] spawn failed\n";
static const char gJobTableFull[] = "[HSH] job table full\n";
static const char gExitRefused[] = "[HSH] exit refused: live background job\n";
static const char gExitInfo[] = "[HSH] HSH exited\n";

static uint64_t
HoHshStringLength(const char *value)
{
    uint64_t length = 0;

    while (value[length] != '\0')
        ++length;

    return length;
}

static void
HoHshMustWrite(const char *buffer, uint64_t length)
{
    if (HoUserWriteStdout(buffer, length) != (int64_t)length)
        HoUserAbort();
}

static void
HoHshMustWriteLiteral(const char *literal)
{
    HoHshMustWrite(literal, HoHshStringLength(literal));
}

static void
HoHshAppendPid(char *buffer, uint64_t *offset, uint64_t capacity, uint32_t pid)
{
    char digits[10];
    uint32_t value = pid;
    uint32_t digitCount = 0;

    do
    {
        digits[digitCount++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    if ((*offset + digitCount) > capacity)
        HoUserAbort();

    while (digitCount != 0U)
    {
        --digitCount;
        buffer[*offset] = digits[digitCount];
        *offset += 1U;
    }
}

static void
HoHshAppendLiteral(char *buffer, uint64_t *offset, uint64_t capacity, const char *literal)
{
    uint64_t literalLength = HoHshStringLength(literal);

    if ((*offset + literalLength) > capacity)
        HoUserAbort();

    for (uint64_t index = 0; index < literalLength; ++index)
    {
        buffer[*offset] = literal[index];
        *offset += 1U;
    }
}

static void
HoHshAppendBoolean(char *buffer, uint64_t *offset, uint64_t capacity, BOOL value)
{
    if ((*offset + 1U) > capacity)
        HoUserAbort();

    buffer[*offset] = value ? '1' : '0';
    *offset += 1U;
}

static BOOL
HoHshLineEquals(const char *line, uint64_t length, const char *literal)
{
    uint64_t literalLength = HoHshStringLength(literal);
    if (length != literalLength)
        return FALSE;

    for (uint64_t index = 0; index < length; ++index)
    {
        if (line[index] != literal[index])
            return FALSE;
    }

    return TRUE;
}

static void
HoHshRecordJob(HO_HSH_JOB *job, uint32_t pid, const char *name, BOOL background)
{
    job->Pid = pid;
    job->Name = name;
    job->Background = background;
    job->Alive = TRUE;
}

static void
HoHshWriteJobStarted(const HO_HSH_JOB *job)
{
    char line[64];
    uint64_t length = 0;

    HoHshAppendLiteral(line, &length, sizeof(line), gStartedPrefix);
    HoHshAppendPid(line, &length, sizeof(line), job->Pid);
    HoHshAppendLiteral(line, &length, sizeof(line), gStartedNamePrefix);
    HoHshAppendLiteral(line, &length, sizeof(line), job->Name);
    HoHshAppendLiteral(line, &length, sizeof(line), gStartedBackgroundPrefix);
    HoHshAppendBoolean(line, &length, sizeof(line), job->Background);
    HoHshAppendLiteral(line, &length, sizeof(line), "\n");
    HoHshMustWrite(line, length);
}

static BOOL
HoHshHasLiveBackgroundJobs(const HO_HSH_JOB *jobs)
{
    for (uint32_t index = 0; index < HO_HSH_MAX_JOBS; ++index)
    {
        if (jobs[index].Alive && jobs[index].Background)
            return TRUE;
    }

    return FALSE;
}

static int32_t
HoHshReserveJobSlot(HO_HSH_JOB *jobs)
{
    for (uint32_t index = 0; index < HO_HSH_MAX_JOBS; ++index)
    {
        if (!jobs[index].Alive)
            return (int32_t)index;
    }

    return -1;
}

int
main(void)
{
    char line[KE_USER_BOOTSTRAP_READLINE_MAX_LENGTH];
    HO_HSH_JOB jobs[HO_HSH_MAX_JOBS] = {0};

    for (;;)
    {
        int64_t status = HoUserWriteStdout(gHshPrompt, sizeof(gHshPrompt) - 1U);
        if (status != (int64_t)(sizeof(gHshPrompt) - 1U))
            HoUserAbort();

        status = HoUserReadLine(line, sizeof(line));
        if (status < 0)
            HoUserAbort();

        if (status == 0)
            continue;

        if (HoHshLineEquals(line, (uint64_t)status, "help"))
        {
            HoHshMustWriteLiteral(gHelpText);
            continue;
        }

        if (HoHshLineEquals(line, (uint64_t)status, "exit"))
        {
            if (HoHshHasLiveBackgroundJobs(jobs))
            {
                HoHshMustWriteLiteral(gExitRefused);
                continue;
            }

            HoHshMustWriteLiteral(gExitInfo);
            return 0;
        }

        if (HoHshLineEquals(line, (uint64_t)status, "calc"))
        {
            int64_t pid = HoUserSpawnBuiltin(KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_CALC,
                                             KE_USER_BOOTSTRAP_SPAWN_FLAG_FOREGROUND);
            if (pid < 0)
            {
                HoHshMustWriteLiteral(gSpawnFailed);
                continue;
            }

            if (HoUserWaitPid((uint64_t)pid) < 0)
                HoUserAbort();

            continue;
        }

        if (HoHshLineEquals(line, (uint64_t)status, "& tick1s"))
        {
            int32_t slotIndex = HoHshReserveJobSlot(jobs);
            if (slotIndex < 0)
            {
                HoHshMustWriteLiteral(gJobTableFull);
                continue;
            }

            int64_t pid = HoUserSpawnBuiltin(KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_TICK1S,
                                             KE_USER_BOOTSTRAP_SPAWN_FLAG_NONE);
            if (pid < 0)
            {
                HoHshMustWriteLiteral(gSpawnFailed);
                continue;
            }

            HoHshRecordJob(&jobs[slotIndex], (uint32_t)pid, gTick1sName, TRUE);
            HoHshWriteJobStarted(&jobs[slotIndex]);
            continue;
        }

        HoHshMustWriteLiteral(gUnknownCommand);
    }
}

#else

static const char gHshPrefix[] = "[HSH] ";
static const char gHshHandoff[] = "[HSH] handoff\n";
static const char gNewLine[] = "\n";

int
main(void)
{
    char line[KE_USER_BOOTSTRAP_READLINE_MAX_LENGTH];
    int64_t status = HoUserWriteStdout(gHshPrompt, sizeof(gHshPrompt) - 1U);
    if (status != (int64_t)(sizeof(gHshPrompt) - 1U))
        HoUserAbort();

    status = HoUserReadLine(line, sizeof(line));
    if (status < 0)
        HoUserAbort();

    if (HoUserWriteStdout(gHshPrefix, sizeof(gHshPrefix) - 1U) != (int64_t)(sizeof(gHshPrefix) - 1U))
        HoUserAbort();

    if (status != 0 && HoUserWriteStdout(line, (uint64_t)status) != status)
        HoUserAbort();

    if (HoUserWriteStdout(gNewLine, sizeof(gNewLine) - 1U) != (int64_t)(sizeof(gNewLine) - 1U))
        HoUserAbort();

    status = HoUserReadLine(line, sizeof(line));
    if (status != -(int64_t)EC_INVALID_STATE)
        HoUserAbort();

    if (HoUserWriteStdout(gHshHandoff, sizeof(gHshHandoff) - 1U) != (int64_t)(sizeof(gHshHandoff) - 1U))
        HoUserAbort();

    HoUserExit(0);
}

#endif
