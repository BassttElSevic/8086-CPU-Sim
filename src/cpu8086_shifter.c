#include "sim/cpu8086_shifter.h"

static uint16_t width_mask(uint8_t width_bits)
{
    return width_bits == 8u ? 0x00FFu : 0xFFFFu;
}

static uint16_t sign_mask(uint8_t width_bits)
{
    return width_bits == 8u ? 0x0080u : 0x8000u;
}

static bool even_parity(uint8_t value)
{
    value ^= (uint8_t)(value >> 4u);
    value ^= (uint8_t)(value >> 2u);
    value ^= (uint8_t)(value >> 1u);
    return (value & 1u) == 0u;
}

static uint16_t szp_flags(uint16_t value, uint8_t width_bits)
{
    uint16_t flags = 0u;

    if ((value & width_mask(width_bits)) == 0u) {
        flags |= CPU8086_FLAG_ZERO;
    }
    if ((value & sign_mask(width_bits)) != 0u) {
        flags |= CPU8086_FLAG_SIGN;
    }
    if (even_parity((uint8_t)value)) {
        flags |= CPU8086_FLAG_PARITY;
    }
    return flags;
}

static bool shift_operation_is_rotate(Cpu8086ShiftOperation operation)
{
    return operation == CPU8086_SHIFT_ROL || operation == CPU8086_SHIFT_ROR ||
           operation == CPU8086_SHIFT_RCL || operation == CPU8086_SHIFT_RCR;
}

Cpu8086ShiftResult cpu8086_shift_execute(Cpu8086ShiftOperation operation,
                                         uint16_t value,
                                         bool carry_in,
                                         uint8_t count,
                                         uint8_t width_bits)
{
    Cpu8086ShiftResult result;
    uint16_t mask;
    uint16_t sign;
    uint16_t working;
    uint16_t iteration;
    bool carry;

    result.value = 0u;
    result.flags = 0u;
    result.flags_mask = 0u;
    if (width_bits != 8u && width_bits != 16u) {
        return result;
    }

    mask = width_mask(width_bits);
    sign = sign_mask(width_bits);
    working = value & mask;
    carry = carry_in;
    if (count == 0u) {
        result.value = working;
        return result;
    }

    for (iteration = 0u; iteration < count; ++iteration) {
        switch (operation) {
        case CPU8086_SHIFT_ROL:
            carry = (working & sign) != 0u;
            working = (uint16_t)(((working << 1u) & mask) | (carry ? 1u : 0u));
            break;

        case CPU8086_SHIFT_ROR:
            carry = (working & 1u) != 0u;
            working = (uint16_t)((working >> 1u) | (carry ? sign : 0u));
            break;

        case CPU8086_SHIFT_RCL:
        {
            bool next_carry = (working & sign) != 0u;
            working = (uint16_t)(((working << 1u) & mask) | (carry ? 1u : 0u));
            carry = next_carry;
            break;
        }

        case CPU8086_SHIFT_RCR:
        {
            bool next_carry = (working & 1u) != 0u;
            working = (uint16_t)((working >> 1u) | (carry ? sign : 0u));
            carry = next_carry;
            break;
        }

        case CPU8086_SHIFT_SHL:
            carry = (working & sign) != 0u;
            working = (uint16_t)((working << 1u) & mask);
            break;

        case CPU8086_SHIFT_SHR:
            carry = (working & 1u) != 0u;
            working = (uint16_t)(working >> 1u);
            break;

        case CPU8086_SHIFT_SAR:
            carry = (working & 1u) != 0u;
            working = (uint16_t)((working >> 1u) | ((working & sign) != 0u ? sign : 0u));
            break;

        default:
            return result;
        }
    }

    result.value = working;
    if (carry) {
        result.flags |= CPU8086_FLAG_CARRY;
    }

    if (shift_operation_is_rotate(operation)) {
        result.flags_mask = CPU8086_FLAG_CARRY;
        if (count == 1u) {
            result.flags_mask |= CPU8086_FLAG_OVERFLOW;
            if (operation == CPU8086_SHIFT_ROL || operation == CPU8086_SHIFT_RCL) {
                if (((working & sign) != 0u) != carry) {
                    result.flags |= CPU8086_FLAG_OVERFLOW;
                }
            } else if (((working & sign) != 0u) != ((working & (sign >> 1u)) != 0u)) {
                result.flags |= CPU8086_FLAG_OVERFLOW;
            }
        }
        return result;
    }

    result.flags |= szp_flags(working, width_bits);
    result.flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_PARITY |
                        CPU8086_FLAG_ZERO | CPU8086_FLAG_SIGN;
    if (count == 1u) {
        result.flags_mask |= CPU8086_FLAG_OVERFLOW;
        if ((operation == CPU8086_SHIFT_SHL &&
             (((working & sign) != 0u) != carry)) ||
            (operation == CPU8086_SHIFT_SHR && (value & sign) != 0u)) {
            result.flags |= CPU8086_FLAG_OVERFLOW;
        }
    }
    return result;
}

uint16_t cpu8086_apply_shift_flags(uint16_t current_flags,
                                   const Cpu8086ShiftResult *result)
{
    if (result == NULL) {
        return cpu8086_normalize_flags(current_flags);
    }

    return cpu8086_normalize_flags((uint16_t)((current_flags &
                                               (uint16_t)~result->flags_mask) |
                                              (result->flags & result->flags_mask)));
}
