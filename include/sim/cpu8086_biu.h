#ifndef SIM_CPU8086_BIU_H
#define SIM_CPU8086_BIU_H

#include "cpu8086_state.h"
#include "sim_kernel.h"

typedef enum {
    CPU8086_BIU_SOURCE_FETCH = 1u,
    CPU8086_BIU_SOURCE_DATA = 2u
} Cpu8086BiuRequestSource;

typedef struct {
    uint8_t prefetch_bytes[2];
    uint8_t prefetch_byte_count;
} Cpu8086BiuResult;

/* EU calls these only while writing its next Cpu8086State. */
bool cpu8086_biu_begin_data(Cpu8086State *state,
                            Cpu8086BiuBusKind kind,
                            bool write,
                            uint32_t address,
                            uint16_t data,
                            uint8_t width_bytes,
                            SimBusLockAction lock_action);
bool cpu8086_biu_take_data(Cpu8086State *state, uint16_t *data, bool *error);
bool cpu8086_biu_data_busy(const Cpu8086State *state);

void cpu8086_biu_drive(const Cpu8086State *state,
                       uint32_t master_id,
                       SimCycleDrive *drive);
void cpu8086_biu_sample(const Cpu8086State *current,
                        Cpu8086State *next,
                        uint32_t master_id,
                        const SimKernelState *kernel_state,
                        const SimCycleResolution *resolution,
                        Cpu8086BiuResult *result);

#endif
