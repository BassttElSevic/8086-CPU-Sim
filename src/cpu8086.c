#include "sim/cpu8086.h"

#include <stdio.h>
#include <string.h>

#include "sim/cpu8086_biu.h"
#include "sim/cpu8086_eu.h"
#include "sim/cpu8086_prefetch.h"
#include "sim/sim_trace.h"

static void cpu8086_reset_module(void *instance, SimState *state)
{
    Cpu8086 *cpu = (Cpu8086 *)instance;
    Cpu8086State reset;
    Cpu8086State *current;
    Cpu8086State *next;

    if (cpu == NULL || state == NULL || !cpu->attached) {
        return;
    }

    cpu8086_state_reset(&reset);
    current = (Cpu8086State *)sim_state_region_current(state, cpu->state_region);
    next = (Cpu8086State *)sim_state_region_next(state, cpu->state_region);
    if (current != NULL && next != NULL) {
        *current = reset;
        *next = reset;
    }
}

static void cpu8086_drive_module(void *instance,
                                 const SimState *current_state,
                                 SimCycleDrive *drive)
{
    Cpu8086 *cpu = (Cpu8086 *)instance;
    const Cpu8086State *state;

    if (cpu == NULL || current_state == NULL || !cpu->attached) {
        return;
    }

    state = (const Cpu8086State *)sim_state_region_current_const(current_state,
                                                                   cpu->state_region);
    if (state == NULL || state->biu.hlda ||
        (state->biu.hold_pending && !state->biu.bus_lock_held &&
         state->biu.data.phase == CPU8086_BIU_TRANSFER_IDLE)) {
        return;
    }
    cpu8086_biu_drive(state, cpu->config.master_id, drive);
}

static bool hold_can_be_acknowledged(const SimKernelState *kernel_state,
                                     const SimCycleResolution *resolution,
                                     const Cpu8086State *next)
{
    if (kernel_state == NULL || resolution == NULL || next == NULL ||
        next->biu.bus_lock_held) {
        return false;
    }

    /*
     * T4 is the edge at which the target commit occurs.  An idle BUS is also
     * safe, but not if this cycle has just started a fresh T1 transaction.
     */
    if (kernel_state->bus.active) {
        return kernel_state->bus.phase == SIM_BUS_T4 &&
               !next->biu.fetch_inflight &&
               (next->biu.data.phase == CPU8086_BIU_TRANSFER_IDLE ||
                next->biu.data.phase == CPU8086_BIU_TRANSFER_COMPLETE);
    }
    return resolution->bus_response.phase == SIM_BUS_IDLE &&
           (next->biu.data.phase == CPU8086_BIU_TRANSFER_IDLE ||
            next->biu.data.phase == CPU8086_BIU_TRANSFER_COMPLETE) &&
           !next->biu.fetch_inflight;
}

static bool hlda_can_be_released(const SimKernelState *kernel_state,
                                 const SimCycleResolution *resolution)
{
    if (kernel_state == NULL || resolution == NULL) {
        return false;
    }

    /*
     * An external master may have started a transaction while HLDA was high.
     * HOLD may fall only after that transaction reaches T4; otherwise the EU
     * could resume while the external master still owns the shared bus.
     */
    if (kernel_state->bus.active) {
        return kernel_state->bus.phase == SIM_BUS_T4;
    }
    return resolution->bus_response.phase == SIM_BUS_IDLE;
}

static void cpu8086_sample_module(void *instance,
                                  const SimState *current_state,
                                  SimState *next_state,
                                  const SimCycleResolution *resolution)
{
    Cpu8086 *cpu = (Cpu8086 *)instance;
    const Cpu8086State *current;
    Cpu8086State *next;
    const SimKernelState *kernel_state;
    Cpu8086EuResult eu_result;
    Cpu8086BiuResult biu_result;
    bool hold_requested;
    bool hold_freezes_cpu;

    if (cpu == NULL || current_state == NULL || next_state == NULL ||
        resolution == NULL || !cpu->attached) {
        return;
    }

    current = (const Cpu8086State *)sim_state_region_current_const(current_state,
                                                                     cpu->state_region);
    next = (Cpu8086State *)sim_state_region_next(next_state, cpu->state_region);
    kernel_state = sim_state_current_kernel(current_state);
    if (current == NULL || next == NULL || kernel_state == NULL) {
        return;
    }

    hold_requested = (resolution->cpu_lines & CPU8086_INPUT_LINE_HOLD) != 0u;
    next->biu.hold_pending = hold_requested;

    /* While HLDA is asserted, the CPU neither advances EU state nor drives BIU. */
    if (current->biu.hlda) {
        next->biu.hlda = hold_requested ||
                         !hlda_can_be_released(kernel_state, resolution);
        return;
    }

    memset(&eu_result, 0, sizeof(eu_result));
    hold_freezes_cpu = current->biu.hold_pending && !current->biu.bus_lock_held &&
                       !current->biu.hlda;
    if (!hold_freezes_cpu) {
        if (cpu8086_eu_accept_external_interrupt(current,
                                                  next,
                                                  kernel_state->irq_lines)) {
            /* INTR/NMI win over a same-boundary pending single-step trap. */
            next->eu.trap_pending = false;
        } else if (!cpu8086_eu_accept_trap(current, next)) {
            cpu8086_eu_evaluate(current,
                                next,
                                (resolution->cpu_lines & CPU8086_INPUT_LINE_TEST_HIGH) != 0u,
                                &eu_result);
        }
    }
    cpu8086_biu_sample(current,
                       next,
                       cpu->config.master_id,
                       kernel_state,
                       resolution,
                       &biu_result);
    /*
     * A request registered by the EU this cycle exists in `next`, not in the
     * old CPU state.  HOLD must not acknowledge during that one-cycle gap.
     */
    if (next->biu.data.phase != CPU8086_BIU_TRANSFER_IDLE || next->biu.fetch_inflight) {
        hold_freezes_cpu = false;
    }
    if (eu_result.trap_boundary) {
        next->eu.trap_pending = true;
    }
    if (eu_result.flush_prefetch) {
        cpu8086_prefetch_clear(&next->prefetch);
        next->biu.fetch_ip = eu_result.flush_fetch_ip;
        next->biu.prefetch_epoch = current->biu.prefetch_epoch + 1u;
    } else if (!cpu8086_prefetch_advance(&current->prefetch,
                                           &next->prefetch,
                                           eu_result.consume_prefetch_byte,
                                           biu_result.prefetch_bytes,
                                           biu_result.prefetch_byte_count)) {
        next->eu.phase = CPU8086_EU_FAULTED;
        next->eu.fault = CPU8086_FAULT_BUS_ERROR;
    }

    if (hold_requested && hold_can_be_acknowledged(kernel_state, resolution, next)) {
        next->biu.hlda = true;
    }
}

static void cpu8086_trace_module(void *instance,
                                 const SimState *current_state,
                                 const SimState *next_state,
                                 SimTrace *trace)
{
    Cpu8086 *cpu = (Cpu8086 *)instance;
    const Cpu8086State *current;
    const Cpu8086State *next;
    char before[SIM_TRACE_TEXT_SIZE];
    char after[SIM_TRACE_TEXT_SIZE];

    if (cpu == NULL || current_state == NULL || next_state == NULL || trace == NULL ||
        !cpu->attached) {
        return;
    }

    current = (const Cpu8086State *)sim_state_region_current_const(current_state,
                                                                     cpu->state_region);
    next = (const Cpu8086State *)sim_state_region_next_const(next_state,
                                                               cpu->state_region);
    if (current == NULL || next == NULL) {
        return;
    }

    if (current->ip != next->ip) {
        (void)snprintf(before, sizeof(before), "%04x", (unsigned)current->ip);
        (void)snprintf(after, sizeof(after), "%04x", (unsigned)next->ip);
        sim_trace_record_state_change(trace, "cpu8086", "IP", before, after);
    }
    if (current->biu.hlda != next->biu.hlda) {
        (void)snprintf(before, sizeof(before), "%u", current->biu.hlda ? 1u : 0u);
        (void)snprintf(after, sizeof(after), "%u", next->biu.hlda ? 1u : 0u);
        sim_trace_record_state_change(trace, "cpu8086", "HLDA", before, after);
    }
}

bool cpu8086_init(Cpu8086 *cpu, const Cpu8086Config *config)
{
    if (cpu == NULL || config == NULL) {
        return false;
    }

    memset(cpu, 0, sizeof(*cpu));
    cpu->config = *config;
    return true;
}

bool cpu8086_attach(Cpu8086 *cpu, SimKernel *kernel)
{
    Cpu8086State reset;
    SimModule module;

    if (cpu == NULL || kernel == NULL || kernel->state == NULL || cpu->attached) {
        return false;
    }

    cpu8086_state_reset(&reset);
    if (!sim_state_add_region(kernel->state,
                              "cpu8086",
                              sizeof(reset),
                              &reset,
                              &cpu->state_region)) {
        return false;
    }

    memset(&module, 0, sizeof(module));
    module.name = "cpu8086";
    module.instance = cpu;
    module.reset = cpu8086_reset_module;
    module.drive = cpu8086_drive_module;
    module.sample = cpu8086_sample_module;
    module.trace = cpu8086_trace_module;
    cpu->attached = true;
    if (!sim_kernel_attach_module(kernel, &module)) {
        cpu->attached = false;
        return false;
    }
    return true;
}

const Cpu8086State *cpu8086_current_state(const Cpu8086 *cpu,
                                           const SimState *state)
{
    if (cpu == NULL || state == NULL || !cpu->attached) {
        return NULL;
    }

    return (const Cpu8086State *)sim_state_region_current_const(state,
                                                                   cpu->state_region);
}

bool cpu8086_hlda(const Cpu8086 *cpu, const SimState *state)
{
    const Cpu8086State *cpu_state = cpu8086_current_state(cpu, state);

    return cpu_state != NULL && cpu_state->biu.hlda;
}

bool cpu8086_esc_request(const Cpu8086 *cpu,
                         const SimState *state,
                         Cpu8086EscRequest *request)
{
    const Cpu8086State *cpu_state = cpu8086_current_state(cpu, state);

    if (cpu_state == NULL || cpu_state->esc.sequence == 0u) {
        return false;
    }
    if (request != NULL) {
        *request = cpu_state->esc;
    }
    return true;
}
