#include "sim/sim_ram.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/sim_state.h"
#include "sim/sim_trace.h"

static bool ram_range_contains(const SimRam *ram, uint32_t address, size_t bytes)
{
    uint64_t base;
    uint64_t end;
    uint64_t first;
    uint64_t last;

    if (ram == NULL || ram->bytes == NULL || bytes == 0u) {
        return false;
    }

    base = (uint64_t)ram->config.physical_base;
    end = base + (uint64_t)ram->config.capacity_bytes;
    first = (uint64_t)address;
    last = first + (uint64_t)bytes;
    return first >= base && last <= end;
}

static size_t access_span(const SimBusRequest *request)
{
    return request->width == SIM_BUS_WORD &&
           (request->byte_enable & SIM_BUS_BYTE_ENABLE_HIGH) != 0u
        ? 2u
        : 1u;
}

static size_t ram_offset(const SimRam *ram, uint32_t physical_address)
{
    return (size_t)((uint64_t)physical_address - (uint64_t)ram->config.physical_base);
}

static bool ram_probe(void *instance, const SimBusRequest *request)
{
    SimRam *ram = (SimRam *)instance;

    return request != NULL && request->kind == SIM_BUS_MEMORY &&
           ram_range_contains(ram, request->address, access_span(request));
}

static void ram_evaluate(void *instance,
                         const SimState *current_state,
                         const SimBusState *transaction,
                         SimBusResponse *response)
{
    SimRam *ram = (SimRam *)instance;
    uint32_t required_waits;
    size_t offset;

    (void)current_state;

    if (ram == NULL || transaction == NULL || response == NULL ||
        !ram_probe(ram, &transaction->request)) {
        if (response != NULL) {
            response->error = true;
        }
        return;
    }

    required_waits = transaction->request.direction == SIM_BUS_READ
        ? ram->config.read_wait_states
        : ram->config.write_wait_states;

    if (transaction->wait_count < required_waits) {
        response->ready = false;
        return;
    }

    response->ready = true;
    if (transaction->request.direction == SIM_BUS_READ) {
        offset = ram_offset(ram, transaction->request.address);
        if (transaction->request.width == SIM_BUS_BYTE) {
            response->data = transaction->request.byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
                ? (uint16_t)((uint16_t)ram->bytes[offset] << 8u)
                : ram->bytes[offset];
        } else if ((transaction->request.byte_enable & SIM_BUS_BYTE_ENABLE_LOW) != 0u) {
            response->data = ram->bytes[offset];
        }
        if (transaction->request.width == SIM_BUS_WORD &&
            (transaction->request.byte_enable & SIM_BUS_BYTE_ENABLE_HIGH) != 0u) {
            response->data |= (uint16_t)((uint16_t)ram->bytes[offset + 1u] << 8u);
        }
    }
}

static void ram_commit(void *instance,
                       const SimState *current_state,
                       SimState *next_state,
                       const SimBusRequest *request,
                       const SimBusResponse *response)
{
    SimRam *ram = (SimRam *)instance;
    size_t offset;

    (void)current_state;
    (void)next_state;
    if (ram == NULL || request == NULL || response == NULL ||
        request->direction != SIM_BUS_WRITE || !response->ready ||
        response->error || !ram_probe(ram, request)) {
        return;
    }

    offset = ram_offset(ram, request->address);
    if (request->width == SIM_BUS_BYTE) {
        ram->bytes[offset] = request->byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
            ? (uint8_t)(request->data >> 8u)
            : (uint8_t)(request->data & 0x00FFu);
    } else if ((request->byte_enable & SIM_BUS_BYTE_ENABLE_LOW) != 0u) {
        ram->bytes[offset] = (uint8_t)(request->data & 0x00FFu);
    }
    if (request->width == SIM_BUS_WORD &&
        (request->byte_enable & SIM_BUS_BYTE_ENABLE_HIGH) != 0u) {
        ram->bytes[offset + 1u] = (uint8_t)(request->data >> 8u);
    }
}

static void ram_trace(void *instance,
                      const SimBusRequest *request,
                      const SimBusResponse *response,
                      SimTrace *trace)
{
    char after[SIM_TRACE_TEXT_SIZE];

    (void)instance;

    if (trace == NULL || request == NULL || response == NULL ||
        request->direction != SIM_BUS_WRITE || !response->ready || response->error) {
        return;
    }

    (void)snprintf(after, sizeof(after), "%04x @ %05x",
                   (unsigned)request->data,
                   (unsigned)request->address);
    sim_trace_record_state_change(trace, "ram", "write", "pending", after);
}

bool sim_ram_init(SimRam *ram, const SimRamConfig *config)
{
    uint64_t end;

    if (ram == NULL || config == NULL || config->capacity_bytes == 0u) {
        return false;
    }

    end = (uint64_t)config->physical_base + (uint64_t)config->capacity_bytes;
    if (end > UINT64_C(0x100000000)) {
        return false;
    }

    memset(ram, 0, sizeof(*ram));
    ram->bytes = (uint8_t *)malloc(config->capacity_bytes);
    if (ram->bytes == NULL) {
        return false;
    }

    ram->config = *config;
    memset(ram->bytes, config->initial_value, config->capacity_bytes);
    return true;
}

void sim_ram_destroy(SimRam *ram)
{
    if (ram == NULL) {
        return;
    }

    free(ram->bytes);
    memset(ram, 0, sizeof(*ram));
}

SimBusTarget sim_ram_bus_target(SimRam *ram, const char *name, uint32_t target_id)
{
    SimBusTarget target;

    memset(&target, 0, sizeof(target));
    target.name = name;
    target.target_id = target_id;
    target.instance = ram;
    target.probe = ram_probe;
    target.evaluate = ram_evaluate;
    target.commit = ram_commit;
    target.trace = ram_trace;
    return target;
}

bool sim_ram_peek_byte(const SimRam *ram, uint32_t physical_address, uint8_t *value)
{
    if (!ram_range_contains(ram, physical_address, 1u)) {
        return false;
    }

    if (value != NULL) {
        *value = ram->bytes[ram_offset(ram, physical_address)];
    }
    return true;
}

bool sim_ram_load_bytes(SimRam *ram,
                        uint32_t physical_address,
                        const uint8_t *source,
                        size_t size)
{
    if (ram == NULL || source == NULL || size == 0u ||
        !ram_range_contains(ram, physical_address, size)) {
        return false;
    }

    memcpy(&ram->bytes[ram_offset(ram, physical_address)], source, size);
    return true;
}
