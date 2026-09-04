#ifndef SIM_SIM_ALU_H
#define SIM_SIM_ALU_H

#include "sim_types.h"

typedef struct {
    uint16_t value;
    bool carry;
    bool parity;
    bool auxiliary_carry;
    bool zero;
    bool sign;
    bool overflow;
} SimAdd16Result;

/* This is combinational logic: it never changes simulator state. */
SimAdd16Result sim_add16(uint16_t a, uint16_t b, bool carry_in);

#endif
