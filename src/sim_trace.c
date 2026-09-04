#include "sim/sim_trace.h"

#include <stdlib.h>
#include <string.h>

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0u) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, capacity - 1u);
    destination[capacity - 1u] = '\0';
}

static const char *bus_kind_name(SimBusKind kind)
{
    switch (kind) {
    case SIM_BUS_MEMORY: return "memory";
    case SIM_BUS_IO: return "io";
    case SIM_BUS_INTERRUPT_ACK: return "interrupt-ack";
    default: return "unknown";
    }
}

static const char *bus_direction_name(SimBusDirection direction)
{
    return direction == SIM_BUS_READ ? "read" : "write";
}

static const char *bus_phase_name(SimBusPhase phase)
{
    switch (phase) {
    case SIM_BUS_IDLE: return "idle";
    case SIM_BUS_T1: return "T1";
    case SIM_BUS_T2: return "T2";
    case SIM_BUS_T3: return "T3";
    case SIM_BUS_WAIT: return "WAIT";
    case SIM_BUS_T4: return "T4";
    default: return "unknown";
    }
}

bool sim_trace_init(SimTrace *trace, size_t initial_capacity)
{
    if (trace == NULL) {
        return false;
    }

    memset(trace, 0, sizeof(*trace));
    trace->enabled = true;
    trace->capacity = initial_capacity;

    if (initial_capacity != 0u) {
        trace->records = (SimTraceRecord *)calloc(initial_capacity,
                                                   sizeof(SimTraceRecord));
        if (trace->records == NULL) {
            trace->capacity = 0u;
            return false;
        }
    }

    return true;
}

void sim_trace_destroy(SimTrace *trace)
{
    if (trace == NULL) {
        return;
    }

    free(trace->records);
    memset(trace, 0, sizeof(*trace));
}

void sim_trace_set_enabled(SimTrace *trace, bool enabled)
{
    if (trace != NULL) {
        trace->enabled = enabled;
    }
}

void sim_trace_set_stream(SimTrace *trace, FILE *stream, bool print_on_commit)
{
    if (trace == NULL) {
        return;
    }

    trace->stream = stream;
    trace->print_on_commit = print_on_commit;
}

void sim_trace_begin(SimTrace *trace, const SimState *state)
{
    if (trace == NULL || !trace->enabled || state == NULL) {
        return;
    }

    memset(&trace->active, 0, sizeof(trace->active));
    trace->active_valid = true;
    trace->active.cycle = state->current_kernel.cycle;
    trace->active.kernel_before = state->current_kernel;
    trace->active.state_digest_before = sim_state_digest(state);
}

void sim_trace_record_request(SimTrace *trace, const SimBusRequest *request)
{
    if (trace == NULL || !trace->active_valid || request == NULL ||
        trace->active.request_count >= SIM_TRACE_MAX_REQUESTS) {
        return;
    }

    trace->active.requests[trace->active.request_count] = *request;
    trace->active.request_count += 1u;
}

void sim_trace_record_response(SimTrace *trace, const SimBusResponse *response)
{
    if (trace == NULL || !trace->active_valid || response == NULL) {
        return;
    }

    trace->active.response = *response;
}

void sim_trace_record_state_change(SimTrace *trace,
                                   const char *owner,
                                   const char *field,
                                   const char *before,
                                   const char *after)
{
    SimTraceStateChange *change;

    if (trace == NULL || !trace->active_valid ||
        trace->active.state_change_count >= SIM_TRACE_MAX_STATE_CHANGES) {
        return;
    }

    change = &trace->active.state_changes[trace->active.state_change_count];
    copy_text(change->owner, sizeof(change->owner), owner);
    copy_text(change->field, sizeof(change->field), field);
    copy_text(change->before, sizeof(change->before), before);
    copy_text(change->after, sizeof(change->after), after);
    trace->active.state_change_count += 1u;
}

void sim_trace_end(SimTrace *trace, const SimState *state)
{
    SimTraceRecord *record;
    size_t new_capacity;
    SimTraceRecord *new_records;

    if (trace == NULL || !trace->active_valid || state == NULL) {
        return;
    }

    trace->active.kernel_after = state->current_kernel;
    trace->active.state_digest_after = sim_state_digest(state);

    if (trace->count == trace->capacity) {
        new_capacity = trace->capacity == 0u ? 16u : trace->capacity * 2u;
        new_records = (SimTraceRecord *)realloc(trace->records,
                                                new_capacity * sizeof(*new_records));
        if (new_records == NULL) {
            trace->active_valid = false;
            return;
        }
        trace->records = new_records;
        trace->capacity = new_capacity;
    }

    record = &trace->records[trace->count];
    *record = trace->active;
    trace->count += 1u;
    trace->active_valid = false;

    if (trace->print_on_commit && trace->stream != NULL) {
        sim_trace_print_record(record, trace->stream);
    }
}

void sim_trace_print_record(const SimTraceRecord *record, FILE *stream)
{
    size_t index;

    if (record == NULL || stream == NULL) {
        return;
    }

    fprintf(stream,
            "cycle=%llu digest=%016llx->%016llx requests=%zu response=%s/%s phase=%s data=%04x\n",
            (unsigned long long)record->cycle,
            (unsigned long long)record->state_digest_before,
            (unsigned long long)record->state_digest_after,
            record->request_count,
            record->response.valid ? "valid" : "none",
            record->response.ready ? "ready" : "not-ready",
            bus_phase_name(record->response.phase),
            (unsigned)record->response.data);

    for (index = 0u; index < record->request_count; ++index) {
        const SimBusRequest *request = &record->requests[index];
        fprintf(stream,
                "  request[%zu] master=%u kind=%s dir=%s width=%u lanes=%u lock=%u/%u address=%08x data=%04x\n",
                index,
                (unsigned)request->master_id,
                bus_kind_name(request->kind),
                bus_direction_name(request->direction),
                (unsigned)request->width,
                (unsigned)request->byte_enable,
                request->lock ? 1u : 0u,
                (unsigned)request->lock_action,
                (unsigned)request->address,
                (unsigned)request->data);
    }

    for (index = 0u; index < record->state_change_count; ++index) {
        const SimTraceStateChange *change = &record->state_changes[index];
        fprintf(stream,
                "  state[%s].%s: %s -> %s\n",
                change->owner,
                change->field,
                change->before,
                change->after);
    }
}

void sim_trace_dump(const SimTrace *trace, FILE *stream)
{
    size_t index;

    if (trace == NULL || stream == NULL) {
        return;
    }

    for (index = 0u; index < trace->count; ++index) {
        sim_trace_print_record(&trace->records[index], stream);
    }
}
