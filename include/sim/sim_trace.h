#ifndef SIM_SIM_TRACE_H
#define SIM_SIM_TRACE_H

#include <stdio.h>

#include "sim_state.h"

#define SIM_TRACE_MAX_REQUESTS 16u
#define SIM_TRACE_MAX_STATE_CHANGES 16u
#define SIM_TRACE_TEXT_SIZE 32u

typedef struct {
    char owner[SIM_TRACE_TEXT_SIZE];
    char field[SIM_TRACE_TEXT_SIZE];
    char before[SIM_TRACE_TEXT_SIZE];
    char after[SIM_TRACE_TEXT_SIZE];
} SimTraceStateChange;

typedef struct {
    uint64_t cycle;
    uint64_t state_digest_before;
    uint64_t state_digest_after;
    SimKernelState kernel_before;
    SimKernelState kernel_after;
    size_t request_count;
    SimBusRequest requests[SIM_TRACE_MAX_REQUESTS];
    SimBusResponse response;
    size_t state_change_count;
    SimTraceStateChange state_changes[SIM_TRACE_MAX_STATE_CHANGES];
} SimTraceRecord;

typedef struct SimTrace {
    bool enabled;
    bool print_on_commit;
    FILE *stream;
    SimTraceRecord *records;
    size_t count;
    size_t capacity;
    SimTraceRecord active;
    bool active_valid;
} SimTrace;

bool sim_trace_init(SimTrace *trace, size_t initial_capacity);
void sim_trace_destroy(SimTrace *trace);
void sim_trace_set_enabled(SimTrace *trace, bool enabled);
void sim_trace_set_stream(SimTrace *trace, FILE *stream, bool print_on_commit);

void sim_trace_begin(SimTrace *trace, const SimState *state);
void sim_trace_record_request(SimTrace *trace, const SimBusRequest *request);
void sim_trace_record_response(SimTrace *trace, const SimBusResponse *response);
void sim_trace_record_state_change(SimTrace *trace,
                                   const char *owner,
                                   const char *field,
                                   const char *before,
                                   const char *after);
void sim_trace_end(SimTrace *trace, const SimState *state);

void sim_trace_print_record(const SimTraceRecord *record, FILE *stream);
void sim_trace_dump(const SimTrace *trace, FILE *stream);

#endif
