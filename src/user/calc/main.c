/**
 * HimuOperatingSystem
 *
 * File: user/calc/main.c
 * Description: Minimal calc REPL for the stable Ex user runtime.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gCalcPrompt[] = "calc> ";

enum
{
    HO_CALC_STACK_CAPACITY = 16U,
};

static const char gCalcResultPrefix[] = "[CALC] result=";
static const char gCalcErrorPrefix[] = "[CALC] error=";
static const char gCalcErrorInvalidToken[] = "invalid_token";
static const char gCalcErrorStackUnderflow[] = "stack_underflow";
static const char gCalcErrorStackOverflow[] = "stack_overflow";
static const char gCalcErrorDivideByZero[] = "divide_by_zero";
static const char gCalcFaultDivideCommand[] = "fault-de";
static const char gCalcFaultPageCommand[] = "fault-pf";
static const char gCalcFaultDivideInfo[] = "[CALC] triggering fault-de\n";
static const char gCalcFaultPageInfo[] = "[CALC] triggering fault-pf\n";

static uint64_t
HoCalcStringLength(const char *value)
{
    uint64_t length = 0;

    while (value[length] != '\0')
        ++length;

    return length;
}

static void
HoCalcMustWrite(const char *buffer, uint64_t length)
{
    if (HoUserWriteStdout(buffer, length) != (int64_t)length)
        HoUserAbort();
}

static void
HoCalcMustWriteLiteral(const char *literal)
{
    HoCalcMustWrite(literal, HoCalcStringLength(literal));
}

static int64_t
HoCalcReadLineBlocking(char *buffer, uint64_t capacity)
{
    for (;;)
    {
        int64_t status = HoUserReadLine(buffer, capacity);
        if (status >= 0)
            return status;

        if (status != -(int64_t)EC_INVALID_STATE)
            HoUserAbort();
    }
}

static BOOL
HoCalcLineEquals(const char *line, uint64_t length, const char *literal)
{
    uint64_t literalLength = HoCalcStringLength(literal);
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
HoCalcAppendLiteral(char *buffer, uint64_t *offset, uint64_t capacity, const char *literal)
{
    uint64_t literalLength = HoCalcStringLength(literal);

    if ((*offset + literalLength) > capacity)
        HoUserAbort();

    for (uint64_t index = 0; index < literalLength; ++index)
    {
        buffer[*offset] = literal[index];
        *offset += 1U;
    }
}

static void
HoCalcAppendInt64(char *buffer, uint64_t *offset, uint64_t capacity, int64_t value)
{
    char digits[20];
    uint64_t magnitude;
    uint32_t digitCount = 0;

    if (value < 0)
    {
        if ((*offset + 1U) > capacity)
            HoUserAbort();

        buffer[*offset] = '-';
        *offset += 1U;
        magnitude = (uint64_t)(-(value + 1)) + 1U;
    }
    else
    {
        magnitude = (uint64_t)value;
    }

    do
    {
        digits[digitCount++] = (char)('0' + (magnitude % 10U));
        magnitude /= 10U;
    } while (magnitude != 0U);

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
HoCalcWriteResult(int64_t value)
{
    char line[64];
    uint64_t length = 0;

    HoCalcAppendLiteral(line, &length, sizeof(line), gCalcResultPrefix);
    HoCalcAppendInt64(line, &length, sizeof(line), value);
    HoCalcAppendLiteral(line, &length, sizeof(line), "\n");
    HoCalcMustWrite(line, length);
}

static void
HoCalcWriteError(const char *message)
{
    char line[64];
    uint64_t length = 0;

    HoCalcAppendLiteral(line, &length, sizeof(line), gCalcErrorPrefix);
    HoCalcAppendLiteral(line, &length, sizeof(line), message);
    HoCalcAppendLiteral(line, &length, sizeof(line), "\n");
    HoCalcMustWrite(line, length);
}

static BOOL
HoCalcIsDigit(char value)
{
    return value >= '0' && value <= '9';
}

static BOOL
HoCalcParseInt64(const char *token, uint64_t length, int64_t *outValue)
{
    const uint64_t int64MaxMagnitude = 9223372036854775807ULL;
    const uint64_t int64MinMagnitude = 9223372036854775808ULL;
    uint64_t index = 0;
    uint64_t magnitude = 0;
    BOOL negative = FALSE;
    uint64_t limit = int64MaxMagnitude;

    if (length == 0)
        return FALSE;

    if (token[0] == '-')
    {
        if (length == 1U)
            return FALSE;

        negative = TRUE;
        index = 1U;
        limit = int64MinMagnitude;
    }

    for (; index < length; ++index)
    {
        uint64_t digit;

        if (!HoCalcIsDigit(token[index]))
            return FALSE;

        digit = (uint64_t)(token[index] - '0');
        if (magnitude > (limit / 10U) || (magnitude == (limit / 10U) && digit > (limit % 10U)))
            return FALSE;

        magnitude = (magnitude * 10U) + digit;
    }

    if (negative)
    {
        if (magnitude == int64MinMagnitude)
            *outValue = (-(int64_t)9223372036854775807LL) - 1LL;
        else
            *outValue = -(int64_t)magnitude;
    }
    else
    {
        *outValue = (int64_t)magnitude;
    }

    return TRUE;
}

static BOOL
HoCalcApplyOperator(int64_t *stack, uint32_t *stackDepth, char operatorToken)
{
    int64_t left;
    int64_t right;
    int64_t result;

    if (*stackDepth < 2U)
    {
        HoCalcWriteError(gCalcErrorStackUnderflow);
        return FALSE;
    }

    left = stack[*stackDepth - 2U];
    right = stack[*stackDepth - 1U];

    switch (operatorToken)
    {
    case '+':
        result = left + right;
        break;
    case '-':
        result = left - right;
        break;
    case '*':
        result = left * right;
        break;
    case '/':
        if (right == 0)
        {
            HoCalcWriteError(gCalcErrorDivideByZero);
            return FALSE;
        }

        result = left / right;
        break;
    default:
        HoCalcWriteError(gCalcErrorInvalidToken);
        return FALSE;
    }

    stack[*stackDepth - 2U] = result;
    *stackDepth -= 1U;
    HoCalcWriteResult(result);
    return TRUE;
}

static HO_NORETURN void
HoCalcTriggerDivideFault(void)
{
    __asm__ volatile("xor %%rdx, %%rdx\n"
                     "mov $1, %%rax\n"
                     "xor %%rcx, %%rcx\n"
                     "idivq %%rcx\n"
                     :
                     :
                     : "rax", "rcx", "rdx", "cc", "memory");

    HoUserAbort();
    __builtin_unreachable();
}

static HO_NORETURN void
HoCalcTriggerPageFault(void)
{
    volatile const uint8_t *guard = (volatile const uint8_t *)HoUserImageStackGuardBase();
    volatile uint8_t value = *guard;

    (void)value;
    HoUserAbort();
    __builtin_unreachable();
}

int
main(void)
{
    char line[EX_USER_READLINE_MAX_LENGTH];
    int64_t stack[HO_CALC_STACK_CAPACITY] = {0};
    uint32_t stackDepth = 0;

    for (;;)
    {
        int64_t status;
        int64_t value;

        HoCalcMustWriteLiteral(gCalcPrompt);
        status = HoCalcReadLineBlocking(line, sizeof(line));
        if (status == 0)
            continue;

        if (status == 1 && line[0] == 'q')
            HoUserExit(0);

        if (HoCalcLineEquals(line, (uint64_t)status, gCalcFaultDivideCommand))
        {
            HoCalcMustWriteLiteral(gCalcFaultDivideInfo);
            HoCalcTriggerDivideFault();
        }

        if (HoCalcLineEquals(line, (uint64_t)status, gCalcFaultPageCommand))
        {
            HoCalcMustWriteLiteral(gCalcFaultPageInfo);
            HoCalcTriggerPageFault();
        }

        if (status == 1 && (line[0] == '+' || line[0] == '-' || line[0] == '*' || line[0] == '/'))
        {
            (void)HoCalcApplyOperator(stack, &stackDepth, line[0]);
            continue;
        }

        if (!HoCalcParseInt64(line, (uint64_t)status, &value))
        {
            HoCalcWriteError(gCalcErrorInvalidToken);
            continue;
        }

        if (stackDepth == HO_CALC_STACK_CAPACITY)
        {
            HoCalcWriteError(gCalcErrorStackOverflow);
            continue;
        }

        stack[stackDepth++] = value;
        HoCalcWriteResult(value);
    }
}
