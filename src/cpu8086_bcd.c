#include "sim/cpu8086_bcd.h"

static bool even_parity(uint8_t value)
{
    value ^= (uint8_t)(value >> 4u);
    value ^= (uint8_t)(value >> 2u);
    value ^= (uint8_t)(value >> 1u);
    return (value & 1u) == 0u;
}

static uint16_t szp_flags(uint8_t value)
{
    uint16_t flags = 0u;

    if (value == 0u) {
        flags |= CPU8086_FLAG_ZERO;
    }
    if ((value & 0x80u) != 0u) {
        flags |= CPU8086_FLAG_SIGN;
    }
    if (even_parity(value)) {
        flags |= CPU8086_FLAG_PARITY;
    }
    return flags;
}

static Cpu8086BcdResult initial_result(uint16_t ax)
{
    Cpu8086BcdResult result;

    result.ax = ax;
    result.flags = 0u;
    result.flags_mask = 0u;
    result.divide_error = false;
    return result;
}

static void set_adjust_flags(Cpu8086BcdResult *result,
                             bool auxiliary_carry,
                             bool carry,
                             uint8_t adjusted_al)
{
    if (result == NULL) {
        return;
    }

    result->flags_mask = CPU8086_FLAG_CARRY |
                         CPU8086_FLAG_PARITY |
                         CPU8086_FLAG_AUXILIARY_CARRY |
                         CPU8086_FLAG_ZERO |
                         CPU8086_FLAG_SIGN;
    result->flags = szp_flags(adjusted_al);
    if (auxiliary_carry) {
        result->flags |= CPU8086_FLAG_AUXILIARY_CARRY;
    }
    if (carry) {
        result->flags |= CPU8086_FLAG_CARRY;
    }
}

Cpu8086BcdResult cpu8086_bcd_execute(Cpu8086BcdOperation operation,
                                     uint16_t ax,
                                     uint16_t current_flags,
                                     uint8_t immediate_base)
{
    Cpu8086BcdResult result;
    uint8_t al;
    uint8_t ah;
    uint8_t original_al;
    bool old_carry;
    bool low_adjust_carry;
    bool auxiliary_carry;
    bool carry;

    result = initial_result(ax);
    al = (uint8_t)ax;
    ah = (uint8_t)(ax >> 8u);
    original_al = al;

    switch (operation) {
    case CPU8086_BCD_DAA:
        old_carry = (current_flags & CPU8086_FLAG_CARRY) != 0u;
        auxiliary_carry = ((al & 0x0Fu) > 9u) ||
                          (current_flags & CPU8086_FLAG_AUXILIARY_CARRY) != 0u;
        low_adjust_carry = false;
        if (auxiliary_carry) {
            low_adjust_carry = (uint16_t)al + 0x06u > 0x00FFu;
            al = (uint8_t)(al + 0x06u);
        }
        carry = al > 0x9Fu || old_carry || low_adjust_carry;
        if (carry) {
            al = (uint8_t)(al + 0x60u);
        }
        result.ax = (uint16_t)((ax & 0xFF00u) | al);
        set_adjust_flags(&result, auxiliary_carry, carry, al);
        return result;

    case CPU8086_BCD_DAS:
        old_carry = (current_flags & CPU8086_FLAG_CARRY) != 0u;
        auxiliary_carry = ((al & 0x0Fu) > 9u) ||
                          (current_flags & CPU8086_FLAG_AUXILIARY_CARRY) != 0u;
        low_adjust_carry = false;
        if (auxiliary_carry) {
            low_adjust_carry = al < 0x06u;
            al = (uint8_t)(al - 0x06u);
        }
        /*
         * The second DAS correction is selected from the pre-adjust AL.
         * A borrow from the low-nibble correction is also observable as CF.
         */
        carry = original_al > 0x99u || old_carry || low_adjust_carry;
        if (carry) {
            al = (uint8_t)(al - 0x60u);
        }
        result.ax = (uint16_t)((ax & 0xFF00u) | al);
        set_adjust_flags(&result, auxiliary_carry, carry, al);
        return result;

    case CPU8086_BCD_AAA:
        auxiliary_carry = ((al & 0x0Fu) > 9u) ||
                          (current_flags & CPU8086_FLAG_AUXILIARY_CARRY) != 0u;
        if (auxiliary_carry) {
            result.ax = (uint16_t)(ax + 0x0106u);
        }
        result.ax &= 0xFF0Fu;
        result.flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_AUXILIARY_CARRY;
        result.flags = auxiliary_carry
            ? CPU8086_FLAG_CARRY | CPU8086_FLAG_AUXILIARY_CARRY
            : 0u;
        return result;

    case CPU8086_BCD_AAS:
        auxiliary_carry = ((al & 0x0Fu) > 9u) ||
                          (current_flags & CPU8086_FLAG_AUXILIARY_CARRY) != 0u;
        if (auxiliary_carry) {
            result.ax = (uint16_t)(ax - 0x0106u);
        }
        result.ax &= 0xFF0Fu;
        result.flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_AUXILIARY_CARRY;
        result.flags = auxiliary_carry
            ? CPU8086_FLAG_CARRY | CPU8086_FLAG_AUXILIARY_CARRY
            : 0u;
        return result;

    case CPU8086_BCD_AAM:
        if (immediate_base == 0u) {
            result.divide_error = true;
            return result;
        }
        result.ax = (uint16_t)(((uint16_t)(al / immediate_base) << 8u) |
                               (uint16_t)(al % immediate_base));
        result.flags_mask = CPU8086_FLAG_PARITY |
                            CPU8086_FLAG_ZERO |
                            CPU8086_FLAG_SIGN;
        result.flags = szp_flags((uint8_t)result.ax);
        return result;

    case CPU8086_BCD_AAD:
        al = (uint8_t)((uint16_t)ah * immediate_base + al);
        result.ax = al;
        result.flags_mask = CPU8086_FLAG_PARITY |
                            CPU8086_FLAG_ZERO |
                            CPU8086_FLAG_SIGN;
        result.flags = szp_flags(al);
        return result;

    default:
        return result;
    }
}

uint16_t cpu8086_apply_bcd_flags(uint16_t current_flags,
                                 const Cpu8086BcdResult *result)
{
    if (result == NULL) {
        return cpu8086_normalize_flags(current_flags);
    }

    return cpu8086_normalize_flags((uint16_t)((current_flags &
                                               (uint16_t)~result->flags_mask) |
                                              (result->flags & result->flags_mask)));
}
