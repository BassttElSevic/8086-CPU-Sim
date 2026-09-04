#ifndef SIM_SIM_REGISTER_H
#define SIM_SIM_REGISTER_H

#include "sim_types.h"

typedef struct {
    uint16_t current;
    uint16_t next;
} SimReg16;

void sim_reg16_init(SimReg16 *reg, uint16_t reset_value);
void sim_reg16_evaluate(SimReg16 *reg, bool load, uint16_t input);
void sim_reg16_commit(SimReg16 *reg, bool reset_asserted, uint16_t reset_value);

#endif
