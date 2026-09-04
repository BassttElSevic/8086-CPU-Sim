#ifndef SIM_SIM_BUS_H
#define SIM_SIM_BUS_H

#include "sim_types.h"

/* Forward declarations keep State -> BUS as a one-way value dependency. */
typedef struct SimState SimState;
typedef struct SimTrace SimTrace;
typedef struct SimCycleDrive SimCycleDrive;
typedef struct SimCycleResolution SimCycleResolution;

#define SIM_BUS_MAX_TARGETS 32u
#define SIM_BUS_NO_TARGET UINT32_MAX
#define SIM_BUS_NO_MASTER UINT32_MAX
#define SIM_BUS_NO_SOURCE UINT32_MAX
#define SIM_BUS_NO_CONTEXT UINT32_MAX
#define SIM_BUS_BYTE_ENABLE_LOW 0x01u
#define SIM_BUS_BYTE_ENABLE_HIGH 0x02u

typedef enum {
    SIM_BUS_MEMORY = 0,
    SIM_BUS_IO,
    SIM_BUS_INTERRUPT_ACK
} SimBusKind;

typedef enum {
    SIM_BUS_READ = 0,
    SIM_BUS_WRITE
} SimBusDirection;

typedef enum {
    SIM_BUS_BYTE = 1,
    SIM_BUS_WORD = 2
} SimBusWidth;

typedef enum {
    SIM_BUS_IDLE = 0,
    SIM_BUS_T1,
    SIM_BUS_T2,
    SIM_BUS_T3,
    SIM_BUS_WAIT,
    SIM_BUS_T4
} SimBusPhase;

typedef enum {
    SIM_BUS_LOCK_NONE = 0,
    SIM_BUS_LOCK_ACQUIRE,
    SIM_BUS_LOCK_HOLD,
    SIM_BUS_LOCK_RELEASE
} SimBusLockAction;

typedef struct {
    bool valid;
    uint32_t master_id;
    SimBusKind kind;
    SimBusDirection direction;
    SimBusWidth width;
    uint32_t address;
    uint16_t data;
    uint8_t byte_enable;
    bool lock;
    SimBusLockAction lock_action;
    uint32_t source_id;
    uint32_t context_id;
} SimBusRequest;

typedef struct {
    bool valid;
    bool ready;
    bool error;
    uint16_t data;
    uint64_t transaction_id;
    uint32_t master_id;
    uint32_t source_id;
    uint32_t context_id;
    uint32_t responder_id;
    SimBusPhase phase;
} SimBusResponse;

/* This is small, edge-triggered transaction state owned by SimState. */
typedef struct {
    bool active;
    uint64_t transaction_id;
    SimBusPhase phase;
    uint32_t selected_target_id;
    uint32_t lock_owner_id;
    uint32_t wait_count;
    SimBusRequest request;
    SimBusResponse response;
} SimBusState;

typedef bool (*SimBusTargetProbeFn)(void *instance,
                                    const SimBusRequest *request);
typedef void (*SimBusTargetEvaluateFn)(void *instance,
                                       const SimState *current_state,
                                       const SimBusState *transaction,
                                       SimBusResponse *response);
typedef void (*SimBusTargetCommitFn)(void *instance,
                                     const SimState *current_state,
                                     SimState *next_state,
                                     const SimBusRequest *request,
                                     const SimBusResponse *response);
typedef void (*SimBusTargetTraceFn)(void *instance,
                                    const SimBusRequest *request,
                                    const SimBusResponse *response,
                                    SimTrace *trace);

typedef struct {
    const char *name;
    uint32_t target_id;
    void *instance;
    SimBusTargetProbeFn probe;
    SimBusTargetEvaluateFn evaluate;
    SimBusTargetCommitFn commit;
    SimBusTargetTraceFn trace;
} SimBusTarget;

/* SimBus contains static wiring and host-side target references, not CPU state. */
typedef struct {
    SimBusTarget targets[SIM_BUS_MAX_TARGETS];
    size_t target_count;
} SimBus;

void sim_bus_init(SimBus *bus);
bool sim_bus_attach_target(SimBus *bus, const SimBusTarget *target);
void sim_bus_reset(SimBus *bus);

/* Called during evaluate. It writes only SimState's next bus state. */
bool sim_bus_resolve(void *instance,
                     const SimState *current_state,
                     SimState *next_state,
                     const SimCycleDrive *drive,
                     SimCycleResolution *resolution);

/* Called by Kernel at the T4 rising edge to apply target side effects. */
void sim_bus_finalize(void *instance,
                      const SimState *current_state,
                      SimState *next_state,
                      SimTrace *trace);

#endif
