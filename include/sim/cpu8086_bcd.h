#ifndef SIM_CPU8086_BCD_H
#define SIM_CPU8086_BCD_H

#include "cpu8086_state.h"

/*
 * The decimal-adjust instructions are kept separate from the binary ALU.
 * Their carry rules use the incoming AF/CF state and their defined FLAGS set
 * differs from ordinary addition and subtraction.
 */
typedef enum {
    CPU8086_BCD_DAA = 0,
    CPU8086_BCD_DAS,
    CPU8086_BCD_AAA,
    CPU8086_BCD_AAS,
    CPU8086_BCD_AAM,
    CPU8086_BCD_AAD
} Cpu8086BcdOperation;

typedef struct {
    uint16_t ax;
    uint16_t flags;
    uint16_t flags_mask;
    bool divide_error;
} Cpu8086BcdResult;

/*
 * Pure 8086 decimal/ASCII-adjust arithmetic.  `immediate_base` is the real
 * second opcode byte for AAM/AAD; it is ignored by the other operations.
 */
Cpu8086BcdResult cpu8086_bcd_execute(Cpu8086BcdOperation operation,
                                     uint16_t ax,
                                     uint16_t current_flags,
                                     uint8_t immediate_base);

uint16_t cpu8086_apply_bcd_flags(uint16_t current_flags,
                                 const Cpu8086BcdResult *result);

#endif
