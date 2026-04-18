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

#if defined(HO_DEMO_TEST_DEMO_SHELL) || defined(HO_DEMO_TEST_USER_FAULT)

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

static const char gHelpText[] = "help\nsysinfo\nmemmap\nps\nexit\ncalc\nfault_de\nfault_pf\n& tick1s\nkill <pid>\n";
static const char gTick1sName[] = "tick1s";
static const char gStartedPrefix[] = "[HSH] started pid=";
static const char gStartedNamePrefix[] = " name=";
static const char gStartedBackgroundPrefix[] = " bg=";
static const char gUnknownCommand[] = "[HSH] unknown command\n";
static const char gSpawnFailed[] = "[HSH] spawn failed\n";
static const char gSysinfoFailed[] = "[HSH] sysinfo failed\n";
static const char gMemmapFailed[] = "[HSH] memmap failed\n";
static const char gPsFailed[] = "[HSH] ps failed\n";
static const char gJobTableFull[] = "[HSH] job table full\n";
static const char gExitRefused[] = "[HSH] exit refused: live background job\n";
static const char gKillRejected[] = "[HSH] kill rejected\n";
static const char gKillFailed[] = "[HSH] kill failed\n";
static const char gKilledPrefix[] = "[HSH] killed pid=";
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
    while (length != 0U)
    {
        uint64_t chunkLength = length;
        if (chunkLength > KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH)
            chunkLength = KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH;

        if (HoUserWriteStdout(buffer, chunkLength) != (int64_t)chunkLength)
            HoUserAbort();

        buffer += chunkLength;
        length -= chunkLength;
    }
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

static BOOL
HoHshTryParseDecimal(const char *buffer, uint64_t length, uint32_t *outValue)
{
    uint64_t value = 0;

    if (outValue == NULL || length == 0U)
        return FALSE;

    for (uint64_t index = 0; index < length; ++index)
    {
        char ch = buffer[index];
        if (ch < '0' || ch > '9')
            return FALSE;

        value = (value * 10U) + (uint64_t)(ch - '0');
        if (value > 0xFFFFFFFFULL)
            return FALSE;
    }

    *outValue = (uint32_t)value;
    return TRUE;
}

static BOOL
HoHshTryParseKillPid(const char *line, uint64_t length, uint32_t *outPid)
{
    uint64_t prefixLength = 5U;

    if (length <= prefixLength)
        return FALSE;

    if (line[0] != 'k' || line[1] != 'i' || line[2] != 'l' || line[3] != 'l' || line[4] != ' ')
        return FALSE;

    return HoHshTryParseDecimal(line + prefixLength, length - prefixLength, outPid);
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

static HO_HSH_JOB *
HoHshFindLiveJobByPid(HO_HSH_JOB *jobs, uint32_t pid)
{
    for (uint32_t index = 0; index < HO_HSH_MAX_JOBS; ++index)
    {
        if (jobs[index].Alive && jobs[index].Pid == pid)
            return &jobs[index];
    }

    return NULL;
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

static void
HoHshWriteKilled(uint32_t pid)
{
    char line[48];
    uint64_t length = 0;

    HoHshAppendLiteral(line, &length, sizeof(line), gKilledPrefix);
    HoHshAppendPid(line, &length, sizeof(line), pid);
    HoHshAppendLiteral(line, &length, sizeof(line), "\n");
    HoHshMustWrite(line, length);
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

        if (HoHshLineEquals(line, (uint64_t)status, "sysinfo"))
        {
            char sysinfoText[EX_SYSINFO_TEXT_MAX_LENGTH];
            int64_t sysinfoLength =
                HoUserQuerySysinfo(EX_SYSINFO_CLASS_OVERVIEW_TEXT, sysinfoText, sizeof(sysinfoText));

            if (sysinfoLength < 0)
            {
                HoHshMustWriteLiteral(gSysinfoFailed);
                continue;
            }

            HoHshMustWrite(sysinfoText, (uint64_t)sysinfoLength);
            continue;
        }

        if (HoHshLineEquals(line, (uint64_t)status, "memmap"))
        {
            char memmapText[EX_SYSINFO_TEXT_MAX_LENGTH];
            int64_t memmapLength = HoUserQuerySysinfo(EX_SYSINFO_CLASS_MEMMAP_TEXT, memmapText, sizeof(memmapText));

            if (memmapLength < 0)
            {
                HoHshMustWriteLiteral(gMemmapFailed);
                continue;
            }

            HoHshMustWrite(memmapText, (uint64_t)memmapLength);
            continue;
        }

        if (HoHshLineEquals(line, (uint64_t)status, "ps"))
        {
            char threadListText[EX_SYSINFO_TEXT_MAX_LENGTH];
            int64_t threadListLength =
                HoUserQuerySysinfo(EX_SYSINFO_CLASS_THREAD_LIST_TEXT, threadListText, sizeof(threadListText));

            if (threadListLength < 0)
            {
                HoHshMustWriteLiteral(gPsFailed);
                continue;
            }

            HoHshMustWrite(threadListText, (uint64_t)threadListLength);
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
            int64_t pid =
                HoUserSpawnBuiltin(KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_CALC, KE_USER_BOOTSTRAP_SPAWN_FLAG_FOREGROUND);
            if (pid < 0)
            {
                HoHshMustWriteLiteral(gSpawnFailed);
                continue;
            }

            if (HoUserWaitPid((uint64_t)pid) < 0)
                HoUserAbort();

            continue;
        }

        if (HoHshLineEquals(line, (uint64_t)status, "fault_de"))
        {
            int64_t pid =
                HoUserSpawnBuiltin(KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_DE, KE_USER_BOOTSTRAP_SPAWN_FLAG_FOREGROUND);
            if (pid < 0)
            {
                HoHshMustWriteLiteral(gSpawnFailed);
                continue;
            }

            if (HoUserWaitPid((uint64_t)pid) < 0)
                HoUserAbort();

            continue;
        }

        if (HoHshLineEquals(line, (uint64_t)status, "fault_pf"))
        {
            int64_t pid =
                HoUserSpawnBuiltin(KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_PF, KE_USER_BOOTSTRAP_SPAWN_FLAG_FOREGROUND);
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

            int64_t pid =
                HoUserSpawnBuiltin(KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_TICK1S, KE_USER_BOOTSTRAP_SPAWN_FLAG_NONE);
            if (pid < 0)
            {
                HoHshMustWriteLiteral(gSpawnFailed);
                continue;
            }

            HoHshRecordJob(&jobs[slotIndex], (uint32_t)pid, gTick1sName, TRUE);
            HoHshWriteJobStarted(&jobs[slotIndex]);
            continue;
        }

        {
            uint32_t killPid = 0;
            if (HoHshTryParseKillPid(line, (uint64_t)status, &killPid))
            {
                HO_HSH_JOB *job = HoHshFindLiveJobByPid(jobs, killPid);
                if (job == NULL)
                {
                    HoHshMustWriteLiteral(gKillRejected);
                    continue;
                }

                if (HoUserKillPid(killPid) < 0)
                {
                    HoHshMustWriteLiteral(gKillFailed);
                    continue;
                }

                job->Alive = FALSE;
                HoHshWriteKilled(killPid);
                continue;
            }
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
