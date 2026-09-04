#include "sim/cpu8086_state.h"

#include <string.h>

void cpu8086_state_reset(Cpu8086State *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->segment[CPU8086_SEG_CS] = 0xFFFFu;
    state->flags = CPU8086_FLAG_ALWAYS_SET;
    state->biu.fetch_ip = state->ip;
    state->eu.phase = CPU8086_EU_NEED_OPCODE;
    state->eu.fault = CPU8086_FAULT_NONE;
}

uint32_t cpu8086_physical_address(uint16_t segment, uint16_t offset)
{
    return (((uint32_t)segment << 4u) + (uint32_t)offset) & 0x000FFFFFu;
}

uint16_t cpu8086_normalize_flags(uint16_t flags)
{
    return (uint16_t)((flags & CPU8086_FLAG_WRITABLE) |
                      CPU8086_FLAG_ALWAYS_SET);
}

uint16_t cpu8086_get_reg16(const Cpu8086State *state, uint8_t register_code)
{
    return state == NULL || register_code >= CPU8086_REG_COUNT
        ? 0u
        : state->general[register_code];
}

void cpu8086_set_reg16(Cpu8086State *state, uint8_t register_code, uint16_t value)
{
    if (state != NULL && register_code < CPU8086_REG_COUNT) {
        state->general[register_code] = value;
    }
}

uint8_t cpu8086_get_reg8(const Cpu8086State *state, uint8_t register_code)
{
    uint16_t value;

    if (state == NULL || register_code >= 8u) {
        return 0u;
    }

    value = state->general[register_code & 0x03u];
    return register_code < 4u ? (uint8_t)value : (uint8_t)(value >> 8u);
}

void cpu8086_set_reg8(Cpu8086State *state, uint8_t register_code, uint8_t value)
{
    uint16_t current;
    uint8_t register_index;

    if (state == NULL || register_code >= 8u) {
        return;
    }

    register_index = register_code & 0x03u;
    current = state->general[register_index];
    if (register_code < 4u) {
        state->general[register_index] = (uint16_t)((current & 0xFF00u) | value);
    } else {
        state->general[register_index] = (uint16_t)((current & 0x00FFu) |
                                                    ((uint16_t)value << 8u));
    }
}
