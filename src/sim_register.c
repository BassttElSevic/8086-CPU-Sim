#include "sim/sim_register.h"

void sim_reg16_init(SimReg16 *reg, uint16_t reset_value)
{
    reg->current = reset_value;
    reg->next = reset_value;
}

void sim_reg16_evaluate(SimReg16 *reg, bool load, uint16_t input)
{
    reg->next = load ? input : reg->current;
}

void sim_reg16_commit(SimReg16 *reg, bool reset_asserted, uint16_t reset_value)
{
    if (reset_asserted) {
        reg->current = reset_value;
    } else {
        reg->current = reg->next;
    }

    reg->next = reg->current;
}
