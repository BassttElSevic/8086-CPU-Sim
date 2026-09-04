#ifndef SIM_CPU8086_DECODER_H
#define SIM_CPU8086_DECODER_H

#include "cpu8086_state.h"

typedef struct {
    Cpu8086Instruction instruction;
    uint8_t opcode;
    uint8_t embedded_register;
    uint8_t immediate_bytes;
    bool needs_modrm;
    bool operand_width_8;
    bool direction_to_reg;
    bool immediate_sign_extend;
} Cpu8086DecodedInstruction;

Cpu8086DecodedInstruction cpu8086_decode_opcode(uint8_t opcode);

#endif
