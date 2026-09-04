#ifndef SIM_CPU8086_EA_H
#define SIM_CPU8086_EA_H

#include "cpu8086_state.h"

typedef struct {
    uint8_t mod;
    uint8_t reg;
    uint8_t rm;
} Cpu8086ModRm;

typedef struct {
    bool is_register;
    uint8_t register_code;
    Cpu8086Segment default_segment;
    uint16_t offset;
} Cpu8086EffectiveAddress;

Cpu8086ModRm cpu8086_decode_modrm(uint8_t byte);
bool cpu8086_effective_address(const Cpu8086State *state,
                               Cpu8086ModRm modrm,
                               int16_t displacement,
                               Cpu8086EffectiveAddress *address);
uint32_t cpu8086_effective_physical_address(const Cpu8086State *state,
                                            const Cpu8086EffectiveAddress *address,
                                            bool segment_override,
                                            Cpu8086Segment override_segment);

#endif
