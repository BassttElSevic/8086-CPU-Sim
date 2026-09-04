#ifndef SIM_SIM_CLOCK_H
#define SIM_SIM_CLOCK_H

#include "sim_types.h"

/* Host-time scheduler for logical rising edges executed by SimKernel. */
typedef struct {
    uint64_t frequency_hz;
    uint64_t last_time_ns;
    uint64_t fractional;
    uint64_t pending_cycles;
    uint64_t max_catchup_cycles;
    bool running;
} SimClock;

bool sim_clock_init(SimClock *clock, uint64_t frequency_hz,
                    uint64_t max_catchup_cycles);
void sim_clock_start(SimClock *clock, uint64_t now_ns);
void sim_clock_stop(SimClock *clock, uint64_t now_ns);
void sim_clock_set_frequency(SimClock *clock, uint64_t frequency_hz,
                             uint64_t now_ns);
uint64_t sim_clock_cycles_due(SimClock *clock, uint64_t now_ns);
/* Take at most max_cycles from the host-time budget without losing the rest. */
uint64_t sim_clock_take_cycles(SimClock *clock, uint64_t now_ns,
                               uint64_t max_cycles);
uint64_t sim_clock_now_ns(void);

#endif
