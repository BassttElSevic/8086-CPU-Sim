#include "sim/cpu8086_alu.h"

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

Cpu8086AluResult cpu8086_alu_execute(Cpu8086AluOperation operation,
                                     uint16_t left,
                                     uint16_t right,
                                     bool carry_in,
                                     uint8_t width_bits)
{
    Cpu8086AluResult result;
    uint32_t extended;
    uint16_t mask;
    uint16_t sign;
    uint16_t value;
    uint16_t carry_value;
    bool update_carry;

    result.value = 0u;
    result.flags = 0u;
    result.flags_mask = 0u;
    if (width_bits != 8u && width_bits != 16u) {
        return result;
    }

    mask = width_mask(width_bits);
    sign = sign_mask(width_bits);
    left &= mask;
    right &= mask;
    carry_value = carry_in ? 1u : 0u;
    update_carry = true;

    switch (operation) {
    case CPU8086_ALU_ADD:
        extended = (uint32_t)left + (uint32_t)right + carry_value;
        value = (uint16_t)(extended & mask);
        result.flags = szp_flags(value, width_bits);
        if (extended > mask) {
            result.flags |= CPU8086_FLAG_CARRY;
        }
        if (((left ^ right ^ value) & 0x0010u) != 0u) {
            result.flags |= CPU8086_FLAG_AUXILIARY_CARRY;
        }
        if (((~(left ^ right) & (left ^ value)) & sign) != 0u) {
            result.flags |= CPU8086_FLAG_OVERFLOW;
        }
        break;

    case CPU8086_ALU_SUB:
        value = (uint16_t)((left - right - carry_value) & mask);
        result.flags = szp_flags(value, width_bits);
        if ((uint32_t)left < (uint32_t)right + carry_value) {
            result.flags |= CPU8086_FLAG_CARRY;
        }
        if (((left ^ right ^ value) & 0x0010u) != 0u) {
            result.flags |= CPU8086_FLAG_AUXILIARY_CARRY;
        }
        if ((((left ^ right) & (left ^ value)) & sign) != 0u) {
            result.flags |= CPU8086_FLAG_OVERFLOW;
        }
        break;

    case CPU8086_ALU_AND:
        value = (uint16_t)(left & right);
        update_carry = false;
        result.flags = szp_flags(value, width_bits);
        result.flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_PARITY |
                            CPU8086_FLAG_ZERO | CPU8086_FLAG_SIGN |
                            CPU8086_FLAG_OVERFLOW;
        break;

    case CPU8086_ALU_OR:
        value = (uint16_t)(left | right);
        update_carry = false;
        result.flags = szp_flags(value, width_bits);
        result.flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_PARITY |
                            CPU8086_FLAG_ZERO | CPU8086_FLAG_SIGN |
                            CPU8086_FLAG_OVERFLOW;
        break;

    case CPU8086_ALU_XOR:
        value = (uint16_t)(left ^ right);
        update_carry = false;
        result.flags = szp_flags(value, width_bits);
        result.flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_PARITY |
                            CPU8086_FLAG_ZERO | CPU8086_FLAG_SIGN |
                            CPU8086_FLAG_OVERFLOW;
        break;

    case CPU8086_ALU_INC:
        extended = (uint32_t)left + 1u;
        value = (uint16_t)(extended & mask);
        result.flags = szp_flags(value, width_bits);
        if (((left ^ 1u ^ value) & 0x0010u) != 0u) {
            result.flags |= CPU8086_FLAG_AUXILIARY_CARRY;
        }
        if (left == (uint16_t)(sign - 1u)) {
            result.flags |= CPU8086_FLAG_OVERFLOW;
        }
        update_carry = false;
        result.flags_mask = CPU8086_FLAG_PARITY | CPU8086_FLAG_AUXILIARY_CARRY |
                            CPU8086_FLAG_ZERO | CPU8086_FLAG_SIGN |
                            CPU8086_FLAG_OVERFLOW;
        break;

    case CPU8086_ALU_DEC:
        value = (uint16_t)((left - 1u) & mask);
        result.flags = szp_flags(value, width_bits);
        if (((left ^ 1u ^ value) & 0x0010u) != 0u) {
            result.flags |= CPU8086_FLAG_AUXILIARY_CARRY;
        }
        if (left == sign) {
            result.flags |= CPU8086_FLAG_OVERFLOW;
        }
        update_carry = false;
        result.flags_mask = CPU8086_FLAG_PARITY | CPU8086_FLAG_AUXILIARY_CARRY |
                            CPU8086_FLAG_ZERO | CPU8086_FLAG_SIGN |
                            CPU8086_FLAG_OVERFLOW;
        break;

    default:
        return result;
    }

    result.value = value;
    if (update_carry) {
        result.flags_mask = CPU8086_FLAG_CARRY | CPU8086_FLAG_PARITY |
                            CPU8086_FLAG_AUXILIARY_CARRY | CPU8086_FLAG_ZERO |
                            CPU8086_FLAG_SIGN | CPU8086_FLAG_OVERFLOW;
    }
    return result;
}

uint16_t cpu8086_apply_alu_flags(uint16_t current_flags,
                                 const Cpu8086AluResult *result)
{
    if (result == NULL) {
        return cpu8086_normalize_flags(current_flags);
    }

    return cpu8086_normalize_flags((uint16_t)((current_flags &
                                               (uint16_t)~result->flags_mask) |
                                              (result->flags & result->flags_mask)));
}
