#ifndef SIM_SIM_KERNEL_H
#define SIM_SIM_KERNEL_H

#include "sim_state.h"
#include "sim_trace.h"

#define SIM_KERNEL_MAX_MODULES 32u
#define SIM_KERNEL_MAX_REQUESTS 16u

typedef struct SimCycleDrive {
    SimBusRequest requests[SIM_KERNEL_MAX_REQUESTS];
    size_t request_count;
    /*
     * CPU-facing level signals sampled by the platform at a cycle boundary.
     * Keep these separate from irq_lines: INTR/NMI have different edge/level
     * rules from pins such as TEST and HOLD.
     */
    uint32_t cpu_lines;
    /* Device interrupt request levels before interrupt-controller arbitration. */
    uint32_t device_irq_lines;
    uint32_t irq_lines;
} SimCycleDrive;

bool sim_cycle_drive_add_request(SimCycleDrive *drive,
                                 const SimBusRequest *request);

typedef struct SimCycleResolution {
    SimBusResponse bus_response;
    uint32_t cpu_lines;
    uint32_t device_irq_lines;
    uint32_t irq_lines;
    bool stop_requested;
} SimCycleResolution;

typedef void (*SimModuleResetFn)(void *instance, SimState *state);
typedef void (*SimModuleDriveFn)(void *instance,
                                 const SimState *current_state,
                                 SimCycleDrive *drive);
typedef void (*SimModuleSampleFn)(void *instance,
                                  const SimState *current_state,
                                  SimState *next_state,
                                  const SimCycleResolution *resolution);
typedef void (*SimModuleTraceFn)(void *instance,
                                 const SimState *current_state,
                                 const SimState *next_state,
                                 SimTrace *trace);
typedef void (*SimModuleDestroyFn)(void *instance);

typedef struct {
    const char *name;
    void *instance;
    SimModuleResetFn reset;
    SimModuleDriveFn drive;
    SimModuleSampleFn sample;
    SimModuleTraceFn trace;
    SimModuleDestroyFn destroy;
} SimModule;

typedef bool (*SimBusResolveFn)(void *instance,
                                const SimState *current_state,
                                SimState *next_state,
                                const SimCycleDrive *drive,
                                SimCycleResolution *resolution);

typedef void (*SimBusFinalizeFn)(void *instance,
                                 const SimState *current_state,
                                 SimState *next_state,
                                 SimTrace *trace);

typedef struct {
    SimState *state;
    SimTrace *trace;
    SimModule modules[SIM_KERNEL_MAX_MODULES];
    size_t module_count;
    SimBusResolveFn resolve_bus;
    SimBusFinalizeFn finalize_bus;
    void *resolve_instance;
} SimKernel;

bool sim_kernel_init(SimKernel *kernel, SimState *state, SimTrace *trace);
void sim_kernel_destroy(SimKernel *kernel);
bool sim_kernel_attach_module(SimKernel *kernel, const SimModule *module);
void sim_kernel_set_bus_resolver(SimKernel *kernel,
                                 SimBusResolveFn resolve,
                                 SimBusFinalizeFn finalize,
                                 void *instance);
void sim_kernel_set_reset(SimKernel *kernel, bool asserted);
void sim_kernel_reset(SimKernel *kernel);
bool sim_kernel_tick(SimKernel *kernel);

#endif
