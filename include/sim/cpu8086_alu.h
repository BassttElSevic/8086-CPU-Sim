#ifndef SIM_CPU8086_ALU_H
#define SIM_CPU8086_ALU_H

#include "cpu8086_state.h"

typedef enum {
    CPU8086_ALU_ADD = 0,
    CPU8086_ALU_SUB,
    CPU8086_ALU_AND,
    CPU8086_ALU_OR,
    CPU8086_ALU_XOR,
    CPU8086_ALU_INC,
    CPU8086_ALU_DEC
} Cpu8086AluOperation;

typedef struct {
    uint16_t value;
    uint16_t flags;
    uint16_t flags_mask;
} Cpu8086AluResult;

Cpu8086AluResult cpu8086_alu_execute(Cpu8086AluOperation operation,
                                     uint16_t left,
                                     uint16_t right,
                                     bool carry_in,
                                     uint8_t width_bits);
uint16_t cpu8086_apply_alu_flags(uint16_t current_flags,
                                 const Cpu8086AluResult *result);

#endif
