#ifndef SIM_CPU8086_SHIFTER_H
#define SIM_CPU8086_SHIFTER_H

#include "cpu8086_state.h"

typedef enum {
    CPU8086_SHIFT_ROL = 0,
    CPU8086_SHIFT_ROR,
    CPU8086_SHIFT_RCL,
    CPU8086_SHIFT_RCR,
    CPU8086_SHIFT_SHL,
    CPU8086_SHIFT_SHR,
    CPU8086_SHIFT_SAR
} Cpu8086ShiftOperation;

typedef struct {
    uint16_t value;
    uint16_t flags;
    uint16_t flags_mask;
} Cpu8086ShiftResult;

/* Models the 8086's unmasked 8-bit count for D0-D3 group-2 instructions. */
Cpu8086ShiftResult cpu8086_shift_execute(Cpu8086ShiftOperation operation,
                                         uint16_t value,
                                         bool carry_in,
                                         uint8_t count,
                                         uint8_t width_bits);
uint16_t cpu8086_apply_shift_flags(uint16_t current_flags,
                                   const Cpu8086ShiftResult *result);

#endif
