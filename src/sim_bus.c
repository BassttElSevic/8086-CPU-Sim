#include "sim/sim_bus.h"

#include <string.h>

#include "sim/sim_kernel.h"
#include "sim/sim_state.h"
#include "sim/sim_trace.h"

static bool request_is_well_formed(const SimBusRequest *request)
{
    if (request == NULL || !request->valid ||
        request->kind > SIM_BUS_INTERRUPT_ACK ||
        request->direction > SIM_BUS_WRITE) {
        return false;
    }

    if (request->width != SIM_BUS_BYTE && request->width != SIM_BUS_WORD) {
        return false;
    }

    if (request->width == SIM_BUS_WORD && (request->address & 1u) != 0u) {
        return false;
    }

    if (request->byte_enable == 0u ||
        (request->byte_enable & ~(SIM_BUS_BYTE_ENABLE_LOW |
                                  SIM_BUS_BYTE_ENABLE_HIGH)) != 0u ||
        request->lock_action > SIM_BUS_LOCK_RELEASE) {
        return false;
    }

    return request->lock == (request->lock_action != SIM_BUS_LOCK_NONE);
}

static bool request_can_start(const SimBusState *current_bus,
                              const SimBusRequest *request)
{
    if (current_bus == NULL || request == NULL) {
        return false;
    }

    if (current_bus->lock_owner_id == SIM_BUS_NO_MASTER) {
        return true;
    }

    return request->master_id == current_bus->lock_owner_id;
}

static SimBusTarget *find_target_by_id(SimBus *bus, uint32_t target_id)
{
    size_t index;

    if (bus == NULL) {
        return NULL;
    }

    for (index = 0u; index < bus->target_count; ++index) {
        if (bus->targets[index].target_id == target_id) {
            return &bus->targets[index];
        }
    }

    return NULL;
}

static SimBusTarget *find_target_for_request(SimBus *bus,
                                              const SimBusRequest *request)
{
    size_t index;

    if (bus == NULL || request == NULL) {
        return NULL;
    }

    for (index = 0u; index < bus->target_count; ++index) {
        SimBusTarget *target = &bus->targets[index];

        if (target->probe(target->instance, request)) {
            return target;
        }
    }

    return NULL;
}

static void set_idle_response(SimCycleResolution *resolution)
{
    memset(&resolution->bus_response, 0, sizeof(resolution->bus_response));
    resolution->bus_response.phase = SIM_BUS_IDLE;
    resolution->bus_response.master_id = SIM_BUS_NO_MASTER;
    resolution->bus_response.source_id = SIM_BUS_NO_SOURCE;
    resolution->bus_response.context_id = SIM_BUS_NO_CONTEXT;
    resolution->bus_response.responder_id = SIM_BUS_NO_TARGET;
}

static void set_bus_response(SimBusResponse *response,
                             uint64_t transaction_id,
                             const SimBusRequest *request,
                             uint32_t target_id,
                             SimBusPhase phase)
{
    memset(response, 0, sizeof(*response));
    response->valid = true;
    response->transaction_id = transaction_id;
    response->master_id = request == NULL ? SIM_BUS_NO_MASTER : request->master_id;
    response->source_id = request == NULL ? SIM_BUS_NO_SOURCE : request->source_id;
    response->context_id = request == NULL ? SIM_BUS_NO_CONTEXT : request->context_id;
    response->responder_id = target_id;
    response->phase = phase;
}

void sim_bus_init(SimBus *bus)
{
    if (bus != NULL) {
        memset(bus, 0, sizeof(*bus));
    }
}

bool sim_bus_attach_target(SimBus *bus, const SimBusTarget *target)
{
    size_t index;

    if (bus == NULL || target == NULL || target->name == NULL ||
        target->probe == NULL || target->evaluate == NULL ||
        target->commit == NULL || bus->target_count >= SIM_BUS_MAX_TARGETS) {
        return false;
    }

    for (index = 0u; index < bus->target_count; ++index) {
        if (bus->targets[index].target_id == target->target_id) {
            return false;
        }
    }

    bus->targets[bus->target_count] = *target;
    bus->target_count += 1u;
    return true;
}

void sim_bus_reset(SimBus *bus)
{
    (void)bus;
}

bool sim_bus_resolve(void *instance,
                     const SimState *current_state,
                     SimState *next_state,
                     const SimCycleDrive *drive,
                     SimCycleResolution *resolution)
{
    SimBus *bus = (SimBus *)instance;
    const SimBusState *current_bus;
    SimBusState *next_bus;
    SimBusTarget *target;
    SimBusRequest request;
    size_t index;
    size_t selected_index;

    if (bus == NULL || current_state == NULL || next_state == NULL ||
        drive == NULL || resolution == NULL) {
        return false;
    }

    set_idle_response(resolution);
    current_bus = &current_state->current_kernel.bus;
    next_bus = &next_state->next_kernel.bus;

    if (current_state->current_kernel.reset_asserted) {
        memset(next_bus, 0, sizeof(*next_bus));
        next_bus->selected_target_id = SIM_BUS_NO_TARGET;
        next_bus->lock_owner_id = SIM_BUS_NO_MASTER;
        return true;
    }

    if (!current_bus->active) {
        selected_index = drive->request_count;
        for (index = 0u; index < drive->request_count; ++index) {
            if (drive->requests[index].valid &&
                request_can_start(current_bus, &drive->requests[index]) &&
                (selected_index == drive->request_count ||
                 drive->requests[index].master_id < drive->requests[selected_index].master_id)) {
                selected_index = index;
            }
        }

        if (selected_index == drive->request_count) {
            memset(next_bus, 0, sizeof(*next_bus));
            next_bus->selected_target_id = SIM_BUS_NO_TARGET;
            next_bus->lock_owner_id = current_bus->lock_owner_id;
            return true;
        }
        request = drive->requests[selected_index];

        target = request_is_well_formed(&request)
            ? find_target_for_request(bus, &request)
            : NULL;
        memset(next_bus, 0, sizeof(*next_bus));
        next_bus->active = true;
        next_bus->transaction_id = current_bus->transaction_id + 1u;
        next_bus->phase = SIM_BUS_T1;
        next_bus->selected_target_id = target == NULL ? SIM_BUS_NO_TARGET : target->target_id;
        next_bus->lock_owner_id = current_bus->lock_owner_id;
        if (request.lock_action == SIM_BUS_LOCK_ACQUIRE &&
            current_bus->lock_owner_id == SIM_BUS_NO_MASTER) {
            next_bus->lock_owner_id = request.master_id;
        }
        next_bus->request = request;
        set_bus_response(&resolution->bus_response,
                         next_bus->transaction_id,
                         &next_bus->request,
                         next_bus->selected_target_id,
                         SIM_BUS_T1);

        next_bus->response = resolution->bus_response;
        return true;
    }

    *next_bus = *current_bus;
    target = find_target_by_id(bus, current_bus->selected_target_id);

    switch (current_bus->phase) {
    case SIM_BUS_T1:
        next_bus->phase = SIM_BUS_T2;
        set_bus_response(&resolution->bus_response,
                         current_bus->transaction_id,
                         &current_bus->request,
                         current_bus->selected_target_id,
                         SIM_BUS_T2);
        next_bus->response = resolution->bus_response;
        return true;

    case SIM_BUS_T2:
        next_bus->phase = SIM_BUS_T3;
        set_bus_response(&resolution->bus_response,
                         current_bus->transaction_id,
                         &current_bus->request,
                         current_bus->selected_target_id,
                         SIM_BUS_T3);
        next_bus->response = resolution->bus_response;
        return true;

    case SIM_BUS_T3:
    case SIM_BUS_WAIT:
        if (target == NULL) {
            set_bus_response(&resolution->bus_response,
                             current_bus->transaction_id,
                             &current_bus->request,
                             SIM_BUS_NO_TARGET,
                             current_bus->phase);
            resolution->bus_response.ready = true;
            /*
             * An 8086 has no architectural bus-error exception for an
             * unimplemented I/O port.  PC software commonly probes optional
             * hardware, so an absent I/O device must complete the cycle; a
             * read observes the open-bus value and a write is ignored.
             * Unmapped memory and INTA remain hard bus errors.
             */
            resolution->bus_response.error = current_bus->request.kind != SIM_BUS_IO;
            if (current_bus->request.kind == SIM_BUS_IO &&
                current_bus->request.direction == SIM_BUS_READ) {
                resolution->bus_response.data = 0xFFFFu;
            }
            next_bus->phase = SIM_BUS_T4;
            if (resolution->bus_response.error &&
                current_bus->request.lock_action != SIM_BUS_LOCK_NONE) {
                next_bus->lock_owner_id = SIM_BUS_NO_MASTER;
            }
            next_bus->response = resolution->bus_response;
            return true;
        }

        set_bus_response(&resolution->bus_response,
                         current_bus->transaction_id,
                         &current_bus->request,
                         target->target_id,
                         current_bus->phase);
        target->evaluate(target->instance,
                         current_state,
                         current_bus,
                         &resolution->bus_response);
        resolution->bus_response.valid = true;
        resolution->bus_response.transaction_id = current_bus->transaction_id;
        resolution->bus_response.master_id = current_bus->request.master_id;
        resolution->bus_response.source_id = current_bus->request.source_id;
        resolution->bus_response.context_id = current_bus->request.context_id;
        resolution->bus_response.responder_id = target->target_id;
        resolution->bus_response.phase = current_bus->phase;

        if (resolution->bus_response.error) {
            resolution->bus_response.ready = true;
        }

        if (resolution->bus_response.ready) {
            next_bus->phase = SIM_BUS_T4;
            if (resolution->bus_response.error &&
                current_bus->request.lock_action != SIM_BUS_LOCK_NONE) {
                next_bus->lock_owner_id = SIM_BUS_NO_MASTER;
            }
        } else {
            next_bus->phase = SIM_BUS_WAIT;
            next_bus->wait_count = current_bus->wait_count + 1u;
        }
        next_bus->response = resolution->bus_response;
        return true;

    case SIM_BUS_T4:
        resolution->bus_response = current_bus->response;
        resolution->bus_response.valid = true;
        resolution->bus_response.ready = true;
        resolution->bus_response.transaction_id = current_bus->transaction_id;
        resolution->bus_response.master_id = current_bus->request.master_id;
        resolution->bus_response.source_id = current_bus->request.source_id;
        resolution->bus_response.context_id = current_bus->request.context_id;
        resolution->bus_response.phase = SIM_BUS_T4;
        memset(next_bus, 0, sizeof(*next_bus));
        next_bus->transaction_id = current_bus->transaction_id;
        next_bus->selected_target_id = SIM_BUS_NO_TARGET;
        next_bus->lock_owner_id = current_bus->request.lock_action == SIM_BUS_LOCK_RELEASE
            ? SIM_BUS_NO_MASTER
            : current_bus->lock_owner_id;
        return true;

    case SIM_BUS_IDLE:
    default:
        return false;
    }
}

void sim_bus_finalize(void *instance,
                      const SimState *current_state,
                      SimState *next_state,
                      SimTrace *trace)
{
    SimBus *bus = (SimBus *)instance;
    const SimBusState *transaction;
    SimBusTarget *target;

    if (bus == NULL || current_state == NULL || next_state == NULL) {
        return;
    }

    transaction = &current_state->current_kernel.bus;
    if (!transaction->active || transaction->phase != SIM_BUS_T4 ||
        !transaction->response.ready || transaction->response.error) {
        return;
    }

    target = find_target_by_id(bus, transaction->selected_target_id);
    if (target == NULL) {
        return;
    }

    target->commit(target->instance,
                   current_state,
                   next_state,
                   &transaction->request,
                   &transaction->response);
    if (target->trace != NULL) {
        target->trace(target->instance,
                      &transaction->request,
                      &transaction->response,
                      trace);
    }
}
