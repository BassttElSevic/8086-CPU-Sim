#ifndef SIM_SIM_STATE_H
#define SIM_SIM_STATE_H

#include "sim_bus.h"

#define SIM_STATE_MAX_REGIONS 32u

typedef size_t SimStateRegionId;

typedef struct SimKernelState {
    uint64_t cycle;
    bool reset_asserted;
    bool paused;
    bool halted;
    uint32_t stop_reason;
    uint32_t cpu_lines;
    uint32_t device_irq_lines;
    uint32_t irq_lines;
    SimBusState bus;
} SimKernelState;

typedef struct {
    const char *name;
    uint8_t *current;
    uint8_t *next;
    uint8_t *reset_value;
    size_t size;
} SimStateRegion;

typedef struct SimState {
    SimKernelState current_kernel;
    SimKernelState next_kernel;
    SimStateRegion regions[SIM_STATE_MAX_REGIONS];
    size_t region_count;
} SimState;

void sim_state_init(SimState *state);
void sim_state_destroy(SimState *state);

bool sim_state_add_region(SimState *state,
                          const char *name,
                          size_t size,
                          const void *reset_value,
                          SimStateRegionId *region_id);
void *sim_state_region_current(SimState *state, SimStateRegionId region_id);
const void *sim_state_region_current_const(const SimState *state,
                                           SimStateRegionId region_id);
void *sim_state_region_next(SimState *state, SimStateRegionId region_id);
const void *sim_state_region_next_const(const SimState *state, SimStateRegionId region_id);
const char *sim_state_region_name(const SimState *state, SimStateRegionId region_id);

const SimKernelState *sim_state_current_kernel(const SimState *state);
SimKernelState *sim_state_next_kernel(SimState *state);

/* Copy current state to next state before a module drive phase. */
void sim_state_begin_cycle(SimState *state);

/* Commit next state at the simulated rising edge. */
void sim_state_commit(SimState *state);

/* Immediate initialization/reset operation, outside a running cycle. */
void sim_state_reset(SimState *state);

uint64_t sim_state_digest(const SimState *state);

#endif
