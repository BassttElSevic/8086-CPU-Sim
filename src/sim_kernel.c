#include "sim/sim_kernel.h"

#include <string.h>

bool sim_cycle_drive_add_request(SimCycleDrive *drive,
                                 const SimBusRequest *request)
{
    if (drive == NULL || request == NULL ||
        drive->request_count >= SIM_KERNEL_MAX_REQUESTS) {
        return false;
    }

    drive->requests[drive->request_count] = *request;
    drive->request_count += 1u;
    return true;
}

bool sim_kernel_init(SimKernel *kernel, SimState *state, SimTrace *trace)
{
    if (kernel == NULL || state == NULL) {
        return false;
    }

    memset(kernel, 0, sizeof(*kernel));
    kernel->state = state;
    kernel->trace = trace;
    return true;
}

void sim_kernel_destroy(SimKernel *kernel)
{
    size_t index;

    if (kernel == NULL) {
        return;
    }

    for (index = 0u; index < kernel->module_count; ++index) {
        if (kernel->modules[index].destroy != NULL) {
            kernel->modules[index].destroy(kernel->modules[index].instance);
        }
    }

    memset(kernel, 0, sizeof(*kernel));
}

bool sim_kernel_attach_module(SimKernel *kernel, const SimModule *module)
{
    if (kernel == NULL || module == NULL || module->name == NULL ||
        kernel->module_count >= SIM_KERNEL_MAX_MODULES) {
        return false;
    }

    kernel->modules[kernel->module_count] = *module;
    kernel->module_count += 1u;
    return true;
}

void sim_kernel_set_bus_resolver(SimKernel *kernel,
                                 SimBusResolveFn resolve,
                                 SimBusFinalizeFn finalize,
                                 void *instance)
{
    if (kernel == NULL) {
        return;
    }

    kernel->resolve_bus = resolve;
    kernel->finalize_bus = finalize;
    kernel->resolve_instance = instance;
}

void sim_kernel_set_reset(SimKernel *kernel, bool asserted)
{
    if (kernel == NULL || kernel->state == NULL) {
        return;
    }

    kernel->state->current_kernel.reset_asserted = asserted;
    kernel->state->next_kernel.reset_asserted = asserted;
}

void sim_kernel_reset(SimKernel *kernel)
{
    size_t index;

    if (kernel == NULL || kernel->state == NULL) {
        return;
    }

    sim_state_reset(kernel->state);
    for (index = 0u; index < kernel->module_count; ++index) {
        if (kernel->modules[index].reset != NULL) {
            kernel->modules[index].reset(kernel->modules[index].instance,
                                         kernel->state);
        }
    }
    sim_state_begin_cycle(kernel->state);
}

bool sim_kernel_tick(SimKernel *kernel)
{
    SimCycleDrive drive;
    SimCycleResolution resolution;
    SimKernelState *next_kernel;
    size_t index;

    if (kernel == NULL || kernel->state == NULL) {
        return false;
    }

    if (kernel->state->current_kernel.paused ||
        kernel->state->current_kernel.halted) {
        return true;
    }

    sim_state_begin_cycle(kernel->state);
    memset(&drive, 0, sizeof(drive));
    memset(&resolution, 0, sizeof(resolution));
    resolution.bus_response.phase = SIM_BUS_IDLE;

    if (kernel->trace != NULL) {
        sim_trace_begin(kernel->trace, kernel->state);
    }

    for (index = 0u; index < kernel->module_count; ++index) {
        if (kernel->modules[index].drive != NULL) {
            kernel->modules[index].drive(kernel->modules[index].instance,
                                         kernel->state,
                                         &drive);
        }
    }

    resolution.cpu_lines = drive.cpu_lines;
    resolution.device_irq_lines = drive.device_irq_lines;
    resolution.irq_lines = drive.irq_lines;

    if (drive.request_count > SIM_KERNEL_MAX_REQUESTS) {
        drive.request_count = SIM_KERNEL_MAX_REQUESTS;
    }

    if (kernel->trace != NULL) {
        for (index = 0u; index < drive.request_count; ++index) {
            sim_trace_record_request(kernel->trace, &drive.requests[index]);
        }
    }

    if (kernel->resolve_bus != NULL &&
        !kernel->resolve_bus(kernel->resolve_instance,
                             kernel->state,
                             kernel->state,
                             &drive,
                             &resolution)) {
        resolution.bus_response.error = true;
        if (kernel->trace != NULL) {
            sim_trace_record_response(kernel->trace, &resolution.bus_response);
            sim_trace_end(kernel->trace, kernel->state);
        }
        return false;
    }

    if (kernel->trace != NULL) {
        sim_trace_record_response(kernel->trace, &resolution.bus_response);
    }

    for (index = 0u; index < kernel->module_count; ++index) {
        if (kernel->modules[index].sample != NULL) {
            kernel->modules[index].sample(kernel->modules[index].instance,
                                          kernel->state,
                                          kernel->state,
                                          &resolution);
        }
    }

    if (kernel->finalize_bus != NULL) {
        kernel->finalize_bus(kernel->resolve_instance,
                             kernel->state,
                             kernel->state,
                             kernel->trace);
    }

    next_kernel = sim_state_next_kernel(kernel->state);
    next_kernel->cycle = kernel->state->current_kernel.cycle + 1u;
    next_kernel->cpu_lines = resolution.cpu_lines;
    next_kernel->device_irq_lines = resolution.device_irq_lines;
    next_kernel->irq_lines = resolution.irq_lines;
    next_kernel->halted = kernel->state->current_kernel.halted ||
                          resolution.stop_requested;

    for (index = 0u; index < kernel->module_count; ++index) {
        if (kernel->modules[index].trace != NULL) {
            kernel->modules[index].trace(kernel->modules[index].instance,
                                         kernel->state,
                                         kernel->state,
                                         kernel->trace);
        }
    }

    sim_state_commit(kernel->state);

    if (kernel->trace != NULL) {
        sim_trace_end(kernel->trace, kernel->state);
    }

    return true;
}
