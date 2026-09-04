#include "sim/sim_state.h"

#include <stdlib.h>
#include <string.h>

static bool valid_region(const SimState *state, SimStateRegionId region_id)
{
    return state != NULL && region_id < state->region_count;
}

void sim_state_init(SimState *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->current_kernel.bus.selected_target_id = SIM_BUS_NO_TARGET;
    state->next_kernel.bus.selected_target_id = SIM_BUS_NO_TARGET;
    state->current_kernel.bus.lock_owner_id = SIM_BUS_NO_MASTER;
    state->next_kernel.bus.lock_owner_id = SIM_BUS_NO_MASTER;
}

void sim_state_destroy(SimState *state)
{
    size_t index;

    if (state == NULL) {
        return;
    }

    for (index = 0u; index < state->region_count; ++index) {
        free(state->regions[index].current);
        free(state->regions[index].next);
        free(state->regions[index].reset_value);
        state->regions[index].current = NULL;
        state->regions[index].next = NULL;
        state->regions[index].reset_value = NULL;
    }

    state->region_count = 0u;
}

bool sim_state_add_region(SimState *state,
                          const char *name,
                          size_t size,
                          const void *reset_value,
                          SimStateRegionId *region_id)
{
    SimStateRegion *region;
    size_t index;

    if (state == NULL || name == NULL || size == 0u ||
        state->region_count >= SIM_STATE_MAX_REGIONS) {
        return false;
    }

    region = &state->regions[state->region_count];
    memset(region, 0, sizeof(*region));
    region->name = name;
    region->size = size;
    region->current = (uint8_t *)malloc(size);
    region->next = (uint8_t *)malloc(size);
    region->reset_value = (uint8_t *)malloc(size);

    if (region->current == NULL || region->next == NULL ||
        region->reset_value == NULL) {
        free(region->current);
        free(region->next);
        free(region->reset_value);
        memset(region, 0, sizeof(*region));
        return false;
    }

    if (reset_value != NULL) {
        memcpy(region->reset_value, reset_value, size);
    } else {
        memset(region->reset_value, 0, size);
    }

    memcpy(region->current, region->reset_value, size);
    memcpy(region->next, region->reset_value, size);
    index = state->region_count;
    state->region_count += 1u;

    if (region_id != NULL) {
        *region_id = index;
    }

    return true;
}

void *sim_state_region_current(SimState *state, SimStateRegionId region_id)
{
    return valid_region(state, region_id) ? state->regions[region_id].current : NULL;
}

const void *sim_state_region_current_const(const SimState *state,
                                           SimStateRegionId region_id)
{
    return valid_region(state, region_id) ? state->regions[region_id].current : NULL;
}

void *sim_state_region_next(SimState *state, SimStateRegionId region_id)
{
    return valid_region(state, region_id) ? state->regions[region_id].next : NULL;
}

const void *sim_state_region_next_const(const SimState *state, SimStateRegionId region_id)
{
    return valid_region(state, region_id) ? state->regions[region_id].next : NULL;
}

const char *sim_state_region_name(const SimState *state, SimStateRegionId region_id)
{
    return valid_region(state, region_id) ? state->regions[region_id].name : NULL;
}

const SimKernelState *sim_state_current_kernel(const SimState *state)
{
    return state == NULL ? NULL : &state->current_kernel;
}

SimKernelState *sim_state_next_kernel(SimState *state)
{
    return state == NULL ? NULL : &state->next_kernel;
}

void sim_state_begin_cycle(SimState *state)
{
    size_t index;

    if (state == NULL) {
        return;
    }

    state->next_kernel = state->current_kernel;
    for (index = 0u; index < state->region_count; ++index) {
        memcpy(state->regions[index].next,
               state->regions[index].current,
               state->regions[index].size);
    }
}

void sim_state_commit(SimState *state)
{
    size_t index;

    if (state == NULL) {
        return;
    }

    state->current_kernel = state->next_kernel;
    for (index = 0u; index < state->region_count; ++index) {
        memcpy(state->regions[index].current,
               state->regions[index].next,
               state->regions[index].size);
    }
}

void sim_state_reset(SimState *state)
{
    size_t index;

    if (state == NULL) {
        return;
    }

    memset(&state->current_kernel, 0, sizeof(state->current_kernel));
    memset(&state->next_kernel, 0, sizeof(state->next_kernel));
    state->current_kernel.reset_asserted = true;
    state->next_kernel.reset_asserted = true;
    state->current_kernel.bus.selected_target_id = SIM_BUS_NO_TARGET;
    state->next_kernel.bus.selected_target_id = SIM_BUS_NO_TARGET;
    state->current_kernel.bus.lock_owner_id = SIM_BUS_NO_MASTER;
    state->next_kernel.bus.lock_owner_id = SIM_BUS_NO_MASTER;

    for (index = 0u; index < state->region_count; ++index) {
        memcpy(state->regions[index].current,
               state->regions[index].reset_value,
               state->regions[index].size);
        memcpy(state->regions[index].next,
               state->regions[index].reset_value,
               state->regions[index].size);
    }
}

uint64_t sim_state_digest(const SimState *state)
{
    const uint8_t *bytes;
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;
    size_t byte_index;

    if (state == NULL) {
        return 0u;
    }

    bytes = (const uint8_t *)&state->current_kernel;
    for (byte_index = 0u; byte_index < sizeof(state->current_kernel); ++byte_index) {
        hash ^= (uint64_t)bytes[byte_index];
        hash *= UINT64_C(1099511628211);
    }

    for (index = 0u; index < state->region_count; ++index) {
        bytes = state->regions[index].current;
        for (byte_index = 0u; byte_index < state->regions[index].size; ++byte_index) {
            hash ^= (uint64_t)bytes[byte_index];
            hash *= UINT64_C(1099511628211);
        }
    }

    return hash;
}
