#include "sim/cpu8086_ea.h"

Cpu8086ModRm cpu8086_decode_modrm(uint8_t byte)
{
    Cpu8086ModRm modrm;

    modrm.mod = (uint8_t)(byte >> 6u);
    modrm.reg = (uint8_t)((byte >> 3u) & 0x07u);
    modrm.rm = (uint8_t)(byte & 0x07u);
    return modrm;
}

bool cpu8086_effective_address(const Cpu8086State *state,
                               Cpu8086ModRm modrm,
                               int16_t displacement,
                               Cpu8086EffectiveAddress *address)
{
    uint16_t base = 0u;

    if (state == NULL || address == NULL || modrm.mod > 3u) {
        return false;
    }

    address->is_register = modrm.mod == 3u;
    address->register_code = modrm.rm;
    address->default_segment = CPU8086_SEG_DS;
    address->offset = 0u;
    if (address->is_register) {
        return true;
    }

    if (modrm.mod == 0u && modrm.rm == 6u) {
        address->offset = (uint16_t)displacement;
        return true;
    }

    switch (modrm.rm) {
    case 0u:
        base = (uint16_t)(state->general[CPU8086_REG_BX] +
                          state->general[CPU8086_REG_SI]);
        break;
    case 1u:
        base = (uint16_t)(state->general[CPU8086_REG_BX] +
                          state->general[CPU8086_REG_DI]);
        break;
    case 2u:
        base = (uint16_t)(state->general[CPU8086_REG_BP] +
                          state->general[CPU8086_REG_SI]);
        address->default_segment = CPU8086_SEG_SS;
        break;
    case 3u:
        base = (uint16_t)(state->general[CPU8086_REG_BP] +
                          state->general[CPU8086_REG_DI]);
        address->default_segment = CPU8086_SEG_SS;
        break;
    case 4u:
        base = state->general[CPU8086_REG_SI];
        break;
    case 5u:
        base = state->general[CPU8086_REG_DI];
        break;
    case 6u:
        base = state->general[CPU8086_REG_BP];
        address->default_segment = CPU8086_SEG_SS;
        break;
    case 7u:
        base = state->general[CPU8086_REG_BX];
        break;
    default:
        return false;
    }

    address->offset = (uint16_t)(base + displacement);
    return true;
}

uint32_t cpu8086_effective_physical_address(const Cpu8086State *state,
                                            const Cpu8086EffectiveAddress *address,
                                            bool segment_override,
                                            Cpu8086Segment override_segment)
{
    Cpu8086Segment segment;

    if (state == NULL || address == NULL || address->is_register ||
        address->default_segment >= CPU8086_SEG_COUNT ||
        (segment_override && override_segment >= CPU8086_SEG_COUNT)) {
        return 0u;
    }

    segment = segment_override ? override_segment : address->default_segment;
    return cpu8086_physical_address(state->segment[segment], address->offset);
}
