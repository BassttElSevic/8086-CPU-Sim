#include "sim/cpu8086_muldiv.h"

static void set_multiply_flags(Cpu8086MulDivResult *result, bool overflow)
{
    if (result == NULL) {
        return;
    }

    result->flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_OVERFLOW;
    if (overflow) {
        result->flags = CPU8086_FLAG_CARRY | CPU8086_FLAG_OVERFLOW;
    }
}

static int64_t sign_extend8(uint8_t value)
{
    return (value & 0x80u) != 0u
        ? (int64_t)value - 0x100LL
        : (int64_t)value;
}

static int64_t sign_extend16(uint16_t value)
{
    return (value & 0x8000u) != 0u
        ? (int64_t)value - 0x10000LL
        : (int64_t)value;
}

Cpu8086MulDivResult cpu8086_muldiv_execute(Cpu8086MulDivOperation operation,
                                           uint16_t ax,
                                           uint16_t dx,
                                           uint16_t operand,
                                           uint8_t width_bits)
{
    Cpu8086MulDivResult result;
    uint32_t unsigned_dividend;
    uint32_t unsigned_quotient;
    uint32_t unsigned_remainder;
    int64_t signed_dividend;
    int64_t signed_divisor;
    int64_t signed_quotient;
    int64_t signed_remainder;

    result.ax = ax;
    result.dx = dx;
    result.flags = 0u;
    result.flags_mask = 0u;
    result.divide_error = false;
    if (width_bits != 8u && width_bits != 16u) {
        result.divide_error = true;
        return result;
    }

    if (width_bits == 8u) {
        operand &= 0x00FFu;
    }

    switch (operation) {
    case CPU8086_MULDIV_MUL:
        if (width_bits == 8u) {
            result.ax = (uint16_t)((uint16_t)(uint8_t)ax * (uint16_t)(uint8_t)operand);
            set_multiply_flags(&result, (result.ax & 0xFF00u) != 0u);
        } else {
            unsigned_dividend = (uint32_t)ax * (uint32_t)operand;
            result.ax = (uint16_t)unsigned_dividend;
            result.dx = (uint16_t)(unsigned_dividend >> 16u);
            set_multiply_flags(&result, result.dx != 0u);
        }
        return result;

    case CPU8086_MULDIV_IMUL:
        if (width_bits == 8u) {
            signed_quotient = sign_extend8((uint8_t)ax) *
                              sign_extend8((uint8_t)operand);
            result.ax = (uint16_t)(int16_t)signed_quotient;
            set_multiply_flags(&result,
                               signed_quotient < -128 || signed_quotient > 127);
        } else {
            signed_quotient = sign_extend16(ax) * sign_extend16(operand);
            result.ax = (uint16_t)signed_quotient;
            result.dx = (uint16_t)((uint64_t)signed_quotient >> 16u);
            set_multiply_flags(&result,
                               signed_quotient < -32768 || signed_quotient > 32767);
        }
        return result;

    case CPU8086_MULDIV_DIV:
        if (operand == 0u) {
            result.divide_error = true;
            return result;
        }
        if (width_bits == 8u) {
            unsigned_dividend = ax;
            unsigned_quotient = unsigned_dividend / operand;
            unsigned_remainder = unsigned_dividend % operand;
            if (unsigned_quotient > 0xFFu) {
                result.divide_error = true;
                return result;
            }
            result.ax = (uint16_t)(unsigned_quotient |
                                   (unsigned_remainder << 8u));
        } else {
            unsigned_dividend = ((uint32_t)dx << 16u) | ax;
            unsigned_quotient = unsigned_dividend / operand;
            unsigned_remainder = unsigned_dividend % operand;
            if (unsigned_quotient > 0xFFFFu) {
                result.divide_error = true;
                return result;
            }
            result.ax = (uint16_t)unsigned_quotient;
            result.dx = (uint16_t)unsigned_remainder;
        }
        return result;

    case CPU8086_MULDIV_IDIV:
        if ((width_bits == 8u && sign_extend8((uint8_t)operand) == 0) ||
            (width_bits == 16u && sign_extend16(operand) == 0)) {
            result.divide_error = true;
            return result;
        }
        if (width_bits == 8u) {
            signed_dividend = sign_extend16(ax);
            signed_divisor = sign_extend8((uint8_t)operand);
            signed_quotient = signed_dividend / signed_divisor;
            signed_remainder = signed_dividend % signed_divisor;
            /* 8086 faults on -128; 80186 later extended the range by one. */
            if (signed_quotient < -127 || signed_quotient > 127) {
                result.divide_error = true;
                return result;
            }
            result.ax = (uint16_t)((uint8_t)signed_quotient |
                                   ((uint16_t)(uint8_t)signed_remainder << 8u));
        } else {
            unsigned_dividend = ((uint32_t)dx << 16u) | ax;
            signed_dividend = (unsigned_dividend & 0x80000000u) != 0u
                ? (int64_t)unsigned_dividend - 0x100000000LL
                : (int64_t)unsigned_dividend;
            signed_divisor = sign_extend16(operand);
            signed_quotient = signed_dividend / signed_divisor;
            signed_remainder = signed_dividend % signed_divisor;
            /* 8086 faults on -32768; 80186 later extended the range by one. */
            if (signed_quotient < -32767 || signed_quotient > 32767) {
                result.divide_error = true;
                return result;
            }
            result.ax = (uint16_t)signed_quotient;
            result.dx = (uint16_t)signed_remainder;
        }
        return result;

    default:
        result.divide_error = true;
        return result;
    }
}

uint16_t cpu8086_apply_muldiv_flags(uint16_t current_flags,
                                    const Cpu8086MulDivResult *result)
{
    if (result == NULL) {
        return cpu8086_normalize_flags(current_flags);
    }

    return cpu8086_normalize_flags((uint16_t)((current_flags &
                                               (uint16_t)~result->flags_mask) |
                                              (result->flags & result->flags_mask)));
}
