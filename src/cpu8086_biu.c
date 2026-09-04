#include "sim/cpu8086_biu.h"

#include <string.h>

static SimBusKind to_sim_bus_kind(Cpu8086BiuBusKind kind)
{
    switch (kind) {
    case CPU8086_BIU_MEMORY:
        return SIM_BUS_MEMORY;
    case CPU8086_BIU_IO:
        return SIM_BUS_IO;
    case CPU8086_BIU_INTERRUPT_ACK:
        return SIM_BUS_INTERRUPT_ACK;
    default:
        return SIM_BUS_MEMORY;
    }
}

static SimBusWidth to_sim_bus_width(uint8_t width_bytes)
{
    return width_bytes == 2u ? SIM_BUS_WORD : SIM_BUS_BYTE;
}

static uint8_t prefetch_width_bytes(const Cpu8086State *state)
{
    uint8_t free_bytes;

    if (state == NULL || state->prefetch.count > CPU8086_PREFETCH_CAPACITY) {
        return 0u;
    }

    free_bytes = (uint8_t)(CPU8086_PREFETCH_CAPACITY - state->prefetch.count);
    if ((state->biu.fetch_ip & 1u) == 0u) {
        return free_bytes >= 2u ? 2u : 0u;
    }
    return free_bytes >= 1u ? 1u : 0u;
}

static uint8_t prefetch_byte_enable(uint16_t fetch_ip, uint8_t width_bytes)
{
    if (width_bytes == 2u) {
        return SIM_BUS_BYTE_ENABLE_LOW | SIM_BUS_BYTE_ENABLE_HIGH;
    }
    return (fetch_ip & 1u) == 0u
        ? SIM_BUS_BYTE_ENABLE_LOW
        : SIM_BUS_BYTE_ENABLE_HIGH;
}

static bool data_transfer_is_split_word(const Cpu8086BiuDataState *data)
{
    return data != NULL && data->width_bytes == 2u && (data->address & 1u) != 0u;
}

static uint32_t next_data_beat_address(const Cpu8086BiuDataState *data)
{
    if (data == NULL) {
        return 0u;
    }

    if (data->kind == CPU8086_BIU_IO) {
        return (data->address + 1u) & 0x0000FFFFu;
    }
    return (data->address + 1u) & 0x000FFFFFu;
}

static SimBusLockAction data_beat_lock_action(const Cpu8086BiuDataState *data)
{
    if (data == NULL) {
        return SIM_BUS_LOCK_NONE;
    }

    if (!data_transfer_is_split_word(data)) {
        return data->lock_action;
    }

    if (data->beat_index == 0u) {
        return data->lock_action == SIM_BUS_LOCK_RELEASE
            ? SIM_BUS_LOCK_HOLD
            : data->lock_action;
    }

    if (data->lock_action == SIM_BUS_LOCK_ACQUIRE) {
        return SIM_BUS_LOCK_HOLD;
    }
    if (data->lock_action == SIM_BUS_LOCK_RELEASE) {
        return SIM_BUS_LOCK_RELEASE;
    }
    return data->lock_action;
}

static void build_data_request(const Cpu8086BiuDataState *data,
                               uint32_t master_id,
                               SimBusRequest *request)
{
    bool split_word;
    bool high_byte_lane;

    if (data == NULL || request == NULL) {
        return;
    }

    split_word = data_transfer_is_split_word(data);
    high_byte_lane = data->width_bytes == 1u && (data->address & 1u) != 0u;
    memset(request, 0, sizeof(*request));
    request->valid = true;
    request->master_id = master_id;
    request->source_id = CPU8086_BIU_SOURCE_DATA;
    request->context_id = 0u;
    request->kind = to_sim_bus_kind(data->kind);
    request->direction = data->write ? SIM_BUS_WRITE : SIM_BUS_READ;
    request->lock_action = data_beat_lock_action(data);
    request->lock = request->lock_action != SIM_BUS_LOCK_NONE;

    if (split_word) {
        request->width = SIM_BUS_BYTE;
        request->address = data->beat_index == 0u
            ? data->address
            : next_data_beat_address(data);
        request->byte_enable = data->beat_index == 0u
            ? SIM_BUS_BYTE_ENABLE_HIGH
            : SIM_BUS_BYTE_ENABLE_LOW;
        if (data->write) {
            request->data = data->beat_index == 0u
                ? (uint16_t)((data->data & 0x00FFu) << 8u)
                : (uint16_t)(data->data >> 8u);
        }
        return;
    }

    request->width = to_sim_bus_width(data->width_bytes);
    request->address = data->address;
    request->byte_enable = data->width_bytes == 2u
        ? (SIM_BUS_BYTE_ENABLE_LOW | SIM_BUS_BYTE_ENABLE_HIGH)
        : (high_byte_lane ? SIM_BUS_BYTE_ENABLE_HIGH : SIM_BUS_BYTE_ENABLE_LOW);
    if (data->write) {
        request->data = high_byte_lane
            ? (uint16_t)((data->data & 0x00FFu) << 8u)
            : data->data;
    }
}

static bool is_own_data(const SimBusState *bus,
                        const Cpu8086BiuDataState *data,
                        uint32_t master_id)
{
    SimBusRequest expected;

    if (bus == NULL || data == NULL || !bus->active) {
        return false;
    }

    build_data_request(data, master_id, &expected);
    if (bus->request.master_id != expected.master_id ||
        bus->request.kind != expected.kind ||
        bus->request.direction != expected.direction ||
        bus->request.width != expected.width ||
        bus->request.address != expected.address ||
        bus->request.data != expected.data ||
        bus->request.byte_enable != expected.byte_enable ||
        bus->request.lock != expected.lock ||
        bus->request.lock_action != expected.lock_action ||
        bus->request.source_id != expected.source_id) {
        return false;
    }

    return true;
}

static bool is_own_fetch(const SimBusState *bus,
                         uint32_t master_id)
{
    return bus != NULL && bus->active &&
           bus->request.master_id == master_id &&
           bus->request.source_id == CPU8086_BIU_SOURCE_FETCH &&
           bus->request.kind == SIM_BUS_MEMORY &&
           bus->request.direction == SIM_BUS_READ;
}

static bool response_is_for(const SimBusResponse *response,
                            uint64_t transaction_id,
                            uint32_t master_id,
                            uint32_t source_id)
{
    return response != NULL && response->valid &&
           response->transaction_id == transaction_id &&
           response->master_id == master_id &&
           response->source_id == source_id;
}

bool cpu8086_biu_begin_data(Cpu8086State *state,
                            Cpu8086BiuBusKind kind,
                            bool write,
                            uint32_t address,
                            uint16_t data,
                            uint8_t width_bytes,
                            SimBusLockAction lock_action)
{
    Cpu8086BiuDataState *transfer;

    if (state == NULL || kind > CPU8086_BIU_INTERRUPT_ACK ||
        (width_bytes != 1u && width_bytes != 2u) ||
        lock_action > SIM_BUS_LOCK_RELEASE ||
        (lock_action == SIM_BUS_LOCK_ACQUIRE && state->biu.bus_lock_held) ||
        ((lock_action == SIM_BUS_LOCK_HOLD || lock_action == SIM_BUS_LOCK_RELEASE) &&
         !state->biu.bus_lock_held) ||
        state->biu.data.phase != CPU8086_BIU_TRANSFER_IDLE) {
        return false;
    }

    transfer = &state->biu.data;
    transfer->phase = CPU8086_BIU_TRANSFER_PENDING;
    transfer->kind = kind;
    transfer->write = write;
    transfer->width_bytes = width_bytes;
    transfer->beat_index = 0u;
    transfer->address = address;
    transfer->data = data;
    transfer->transaction_id = 0u;
    transfer->lock_action = lock_action;
    transfer->error = false;
    return true;
}

bool cpu8086_biu_take_data(Cpu8086State *state, uint16_t *data, bool *error)
{
    Cpu8086BiuDataState *transfer;

    if (state == NULL || state->biu.data.phase != CPU8086_BIU_TRANSFER_COMPLETE) {
        return false;
    }

    transfer = &state->biu.data;
    if (data != NULL) {
        *data = transfer->data;
    }
    if (error != NULL) {
        *error = transfer->error;
    }
    memset(transfer, 0, sizeof(*transfer));
    return true;
}

bool cpu8086_biu_data_busy(const Cpu8086State *state)
{
    return state != NULL && state->biu.data.phase != CPU8086_BIU_TRANSFER_IDLE;
}

void cpu8086_biu_drive(const Cpu8086State *state,
                       uint32_t master_id,
                       SimCycleDrive *drive)
{
    SimBusRequest request;
    uint8_t width_bytes;

    if (state == NULL || drive == NULL) {
        return;
    }

    if (state->biu.data.phase == CPU8086_BIU_TRANSFER_PENDING) {
        build_data_request(&state->biu.data, master_id, &request);
        (void)sim_cycle_drive_add_request(drive, &request);
        return;
    }

    if (state->biu.data.phase != CPU8086_BIU_TRANSFER_IDLE ||
        state->biu.fetch_inflight || state->biu.bus_lock_held ||
        state->prefetch.count >= CPU8086_PREFETCH_CAPACITY ||
        state->eu.phase == CPU8086_EU_HALTED ||
        state->eu.phase == CPU8086_EU_FAULTED) {
        return;
    }

    width_bytes = prefetch_width_bytes(state);
    if (width_bytes == 0u) {
        return;
    }

    memset(&request, 0, sizeof(request));
    request.valid = true;
    request.master_id = master_id;
    request.source_id = CPU8086_BIU_SOURCE_FETCH;
    request.context_id = state->biu.prefetch_epoch;
    request.kind = SIM_BUS_MEMORY;
    request.direction = SIM_BUS_READ;
    request.width = to_sim_bus_width(width_bytes);
    request.address = cpu8086_physical_address(state->segment[CPU8086_SEG_CS],
                                                state->biu.fetch_ip);
    request.byte_enable = prefetch_byte_enable(state->biu.fetch_ip, width_bytes);
    (void)sim_cycle_drive_add_request(drive, &request);
}

void cpu8086_biu_sample(const Cpu8086State *current,
                        Cpu8086State *next,
                        uint32_t master_id,
                        const SimKernelState *kernel_state,
                        const SimCycleResolution *resolution,
                        Cpu8086BiuResult *result)
{
    const SimBusState *bus;
    SimBusRequest data_request;

    if (current == NULL || next == NULL || kernel_state == NULL ||
        resolution == NULL || result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    bus = &kernel_state->bus;
    /* A fetch can have entered T1 before the EU queues a same-address data request. */
    if (!current->biu.fetch_inflight && is_own_fetch(bus, master_id) &&
        bus->phase == SIM_BUS_T1) {
        next->biu.fetch_inflight = true;
        next->biu.fetch_address = bus->request.address;
        next->biu.fetch_width_bytes = (uint8_t)bus->request.width;
        next->biu.fetch_byte_enable = bus->request.byte_enable;
        next->biu.fetch_epoch = bus->request.context_id;
        next->biu.fetch_transaction_id = bus->transaction_id;
    }

    if (current->biu.data.phase == CPU8086_BIU_TRANSFER_PENDING &&
        is_own_data(bus, &current->biu.data, master_id) && bus->phase == SIM_BUS_T1) {
        next->biu.data.phase = CPU8086_BIU_TRANSFER_INFLIGHT;
        next->biu.data.transaction_id = bus->transaction_id;
    }

    if (current->biu.data.phase == CPU8086_BIU_TRANSFER_INFLIGHT &&
        (bus->phase == SIM_BUS_T3 || bus->phase == SIM_BUS_WAIT) &&
        resolution->bus_response.ready &&
        response_is_for(&resolution->bus_response,
                        current->biu.data.transaction_id,
                        master_id,
                        CPU8086_BIU_SOURCE_DATA)) {
        build_data_request(&current->biu.data, master_id, &data_request);
        next->biu.data.error = resolution->bus_response.error;
        if (resolution->bus_response.error &&
            data_request.lock_action != SIM_BUS_LOCK_NONE) {
            next->biu.bus_lock_held = false;
        } else if (data_request.lock_action == SIM_BUS_LOCK_ACQUIRE) {
            next->biu.bus_lock_held = true;
        } else if (data_request.lock_action == SIM_BUS_LOCK_RELEASE) {
            next->biu.bus_lock_held = false;
        }

        if (resolution->bus_response.error) {
            next->biu.data.phase = CPU8086_BIU_TRANSFER_COMPLETE;
            return;
        }

        if (data_transfer_is_split_word(&current->biu.data) &&
            current->biu.data.beat_index == 0u) {
            if (!current->biu.data.write) {
                next->biu.data.data =
                    (uint16_t)((resolution->bus_response.data >> 8u) & 0x00FFu);
            }
            next->biu.data.beat_index = 1u;
            next->biu.data.transaction_id = 0u;
            next->biu.data.phase = CPU8086_BIU_TRANSFER_PENDING;
            return;
        }

        next->biu.data.phase = CPU8086_BIU_TRANSFER_COMPLETE;
        if (!current->biu.data.write) {
            if (data_transfer_is_split_word(&current->biu.data)) {
                next->biu.data.data = (uint16_t)(current->biu.data.data |
                    ((resolution->bus_response.data & 0x00FFu) << 8u));
            } else if (data_request.byte_enable == SIM_BUS_BYTE_ENABLE_HIGH) {
                next->biu.data.data =
                    (uint16_t)((resolution->bus_response.data >> 8u) & 0x00FFu);
            } else {
                next->biu.data.data = resolution->bus_response.data;
            }
        }
        return;
    }

    /* A pre-existing fetch can finish while the EU has already queued data. */
    if (current->biu.fetch_inflight &&
        (bus->phase == SIM_BUS_T3 || bus->phase == SIM_BUS_WAIT) &&
        resolution->bus_response.ready &&
        response_is_for(&resolution->bus_response,
                        current->biu.fetch_transaction_id,
                        master_id,
                        CPU8086_BIU_SOURCE_FETCH)) {
        next->biu.fetch_inflight = false;
        next->biu.fetch_address = 0u;
        next->biu.fetch_width_bytes = 0u;
        next->biu.fetch_byte_enable = 0u;
        next->biu.fetch_transaction_id = 0u;
        if (current->biu.fetch_epoch != current->biu.prefetch_epoch) {
            return;
        }
        if (resolution->bus_response.error) {
            next->eu.phase = CPU8086_EU_FAULTED;
            next->eu.fault = CPU8086_FAULT_BUS_ERROR;
            return;
        }

        result->prefetch_byte_count = current->biu.fetch_width_bytes;
        if (current->biu.fetch_width_bytes == 2u) {
            result->prefetch_bytes[0u] =
                (uint8_t)(resolution->bus_response.data & 0x00FFu);
            result->prefetch_bytes[1u] =
                (uint8_t)(resolution->bus_response.data >> 8u);
        } else {
            result->prefetch_bytes[0u] =
                current->biu.fetch_byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
                ? (uint8_t)(resolution->bus_response.data >> 8u)
                : (uint8_t)(resolution->bus_response.data & 0x00FFu);
        }
        next->biu.fetch_ip = (uint16_t)(current->biu.fetch_ip +
                                         current->biu.fetch_width_bytes);
        return;
    }

}
