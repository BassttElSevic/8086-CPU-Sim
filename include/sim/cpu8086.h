#ifndef SIM_CPU8086_H
#define SIM_CPU8086_H

#include "cpu8086_state.h"
#include "sim_kernel.h"

enum {
    CPU8086_IRQ_LINE_INTR = 0x00000001u,
    CPU8086_IRQ_LINE_NMI = 0x00000002u
};

/* Level inputs carried in SimCycleDrive.cpu_lines, not in irq_lines. */
enum {
    CPU8086_INPUT_LINE_TEST_HIGH = 0x00000001u,
    CPU8086_INPUT_LINE_HOLD = 0x00000002u
};

typedef struct {
    uint32_t master_id;
} Cpu8086Config;

typedef struct {
    Cpu8086Config config;
    SimStateRegionId state_region;
    bool attached;
} Cpu8086;

bool cpu8086_init(Cpu8086 *cpu, const Cpu8086Config *config);
bool cpu8086_attach(Cpu8086 *cpu, SimKernel *kernel);

const Cpu8086State *cpu8086_current_state(const Cpu8086 *cpu,
                                           const SimState *state);
bool cpu8086_hlda(const Cpu8086 *cpu, const SimState *state);
bool cpu8086_esc_request(const Cpu8086 *cpu,
                         const SimState *state,
                         Cpu8086EscRequest *request);

#endif
