#ifndef SIM_CPU8086_EU_H
#define SIM_CPU8086_EU_H

#include "cpu8086_state.h"

typedef struct {
    bool consume_prefetch_byte;
    bool flush_prefetch;
    uint16_t flush_fetch_ip;
    bool trap_boundary;
} Cpu8086EuResult;

/* Evaluates one EU microstep against current state; it does not mutate a queue. */
void cpu8086_eu_evaluate(const Cpu8086State *current,
                         Cpu8086State *next,
                         bool test_high,
                         Cpu8086EuResult *result);

/* Returns true after replacing an instruction-boundary state with an external entry. */
bool cpu8086_eu_accept_external_interrupt(const Cpu8086State *current,
                                          Cpu8086State *next,
                                          uint32_t irq_lines);
bool cpu8086_eu_accept_trap(const Cpu8086State *current,
                            Cpu8086State *next);

#endif
