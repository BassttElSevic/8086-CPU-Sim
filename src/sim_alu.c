#include "sim/sim_alu.h"

static bool even_parity8(uint8_t value)
{
    unsigned ones = 0u;
    unsigned bit;

    for (bit = 0u; bit < 8u; ++bit) {
        ones += (unsigned)((value >> bit) & 1u);
    }

    return (ones % 2u) == 0u;
}

SimAdd16Result sim_add16(uint16_t a, uint16_t b, bool carry_in)
{
    SimAdd16Result result;
    uint32_t sum = (uint32_t)a + (uint32_t)b + (carry_in ? 1u : 0u);

    result.value = (uint16_t)sum;
    result.carry = (sum & 0x10000u) != 0u;
    result.auxiliary_carry = (((uint32_t)a & 0x0fu) +
                              ((uint32_t)b & 0x0fu) +
                              (carry_in ? 1u : 0u)) > 0x0fu;
    result.zero = result.value == 0u;
    result.sign = (result.value & 0x8000u) != 0u;
    result.parity = even_parity8((uint8_t)result.value);
    result.overflow = (((~((uint32_t)a ^ (uint32_t)b)) &
                        ((uint32_t)a ^ (uint32_t)result.value) &
                        0x8000u) != 0u);

    return result;
}
