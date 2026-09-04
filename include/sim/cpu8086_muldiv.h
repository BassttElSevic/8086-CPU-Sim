#ifndef SIM_CPU8086_MULDIV_H
#define SIM_CPU8086_MULDIV_H

#include "cpu8086_state.h"

typedef enum {
    CPU8086_MULDIV_MUL = 0,
    CPU8086_MULDIV_IMUL,
    CPU8086_MULDIV_DIV,
    CPU8086_MULDIV_IDIV
} Cpu8086MulDivOperation;

typedef struct {
    uint16_t ax;
    uint16_t dx;
    uint16_t flags;
    uint16_t flags_mask;
    bool divide_error;
} Cpu8086MulDivResult;

/* Pure arithmetic for the F6/F7 MUL, IMUL, DIV and IDIV subfunctions. */
Cpu8086MulDivResult cpu8086_muldiv_execute(Cpu8086MulDivOperation operation,
                                           uint16_t ax,
                                           uint16_t dx,
                                           uint16_t operand,
                                           uint8_t width_bits);
uint16_t cpu8086_apply_muldiv_flags(uint16_t current_flags,
                                    const Cpu8086MulDivResult *result);

#endif
