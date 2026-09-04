#include "sim/sim_dma.h"

#include <string.h>

#include "sim/sim_state.h"

enum {
    DMA_PORT_BASE = 0x00u,
    DMA_PORT_END = 0x0Fu,
    DMA_PAGE_CHANNEL_0 = 0x87u,
    DMA_PAGE_CHANNEL_1 = 0x83u,
    DMA_PAGE_CHANNEL_2 = 0x81u,
    DMA_PAGE_CHANNEL_3 = 0x82u,
    DMA_COMMAND_DISABLE = 0x04u,
    DMA_COMMAND_MEMORY_TO_MEMORY = 0x01u,
    DMA_COMMAND_CHANNEL_0_ADDRESS_HOLD = 0x02u,
    DMA_COMMAND_ROTATING_PRIORITY = 0x10u,
    DMA_MODE_AUTOINIT = 0x10u,
    DMA_MODE_DECREMENT = 0x20u
};

static uint8_t request_byte(const SimBusRequest *request)
{
    return request->byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
        ? (uint8_t)(request->data >> 8u) : (uint8_t)request->data;
}

static uint16_t response_byte(const SimBusRequest *request, uint8_t value)
{
    return request->byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
        ? (uint16_t)((uint16_t)value << 8u) : value;
}

static int page_channel(uint16_t port)
{
    switch (port) {
    case DMA_PAGE_CHANNEL_0: return 0;
    case DMA_PAGE_CHANNEL_1: return 1;
    case DMA_PAGE_CHANNEL_2: return 2;
    case DMA_PAGE_CHANNEL_3: return 3;
    default: return -1;
    }
}

static bool dma_io(const SimBusRequest *request)
{
    return request != NULL && request->kind == SIM_BUS_IO &&
           (request->address <= DMA_PORT_END ||
            page_channel((uint16_t)request->address) >= 0);
}

static bool probe(void *instance, const SimBusRequest *request)
{
    return instance != NULL && dma_io(request);
}

static const SimDmaState *state_current(const SimDma *dma, const SimState *state)
{
    return dma == NULL || state == NULL || !dma->attached ? NULL :
        (const SimDmaState *)sim_state_region_current_const(state, dma->state_region);
}

static bool channel_requested(const SimDma *dma, const SimDmaState *state, unsigned channel)
{
    uint8_t line;
    uint8_t mode;

    if (dma == NULL || state == NULL || channel >= SIM_DMA_CHANNEL_COUNT ||
        (state->mask & (1u << channel)) != 0u) return false;
    line = (uint8_t)(1u << channel);
    mode = state->channel[channel].mode & 0xC0u;
    return (dma->dreq_lines & line) != 0u ||
           (mode == SIM_DMA_MODE_BLOCK && (state->software_request & line) != 0u);
}

static int select_channel(const SimDma *dma, const SimDmaState *state)
{
    unsigned first = 0u;

    if (dma == NULL || state == NULL || (state->command & DMA_COMMAND_DISABLE) != 0u) return -1;
    if ((state->command & DMA_COMMAND_ROTATING_PRIORITY) != 0u) {
        first = (unsigned)((state->last_channel + 1u) % SIM_DMA_CHANNEL_COUNT);
    }
    for (unsigned offset = 0u; offset < SIM_DMA_CHANNEL_COUNT; ++offset) {
        unsigned channel = (first + offset) % SIM_DMA_CHANNEL_COUNT;
        if (channel_requested(dma, state, channel)) return (int)channel;
    }
    return -1;
}

static uint32_t physical_address(const SimDmaChannelState *channel)
{
    return ((uint32_t)channel->page << 16u) | channel->current_address;
}

static SimBusRequest make_request(const SimDma *dma, const SimDmaState *state,
                                  SimBusKind kind, SimBusDirection direction)
{
    SimBusRequest request = {0};
    const SimDmaChannelBinding *binding = &dma->binding[state->active_channel];

    request.valid = true;
    request.master_id = SIM_DMA_MASTER_ID;
    request.kind = kind;
    request.direction = direction;
    request.width = SIM_BUS_BYTE;
    request.byte_enable = SIM_BUS_BYTE_ENABLE_LOW;
    request.source_id = 8u;
    request.context_id = state->active_channel;
    request.address = kind == SIM_BUS_MEMORY
        ? physical_address(&state->channel[state->active_channel]) : binding->io_port;
    request.data = state->temporary;
    return request;
}

static SimBusRequest make_memory_to_memory_request(const SimDma *dma,
                                                    const SimDmaState *state,
                                                    SimBusDirection direction)
{
    SimBusRequest request = {0};
    unsigned channel = direction == SIM_BUS_READ ? 0u : 1u;

    (void)dma;
    request.valid = true;
    request.master_id = SIM_DMA_MASTER_ID;
    request.kind = SIM_BUS_MEMORY;
    request.direction = direction;
    request.width = SIM_BUS_BYTE;
    request.byte_enable = SIM_BUS_BYTE_ENABLE_LOW;
    request.source_id = 8u;
    request.context_id = channel;
    request.address = physical_address(&state->channel[channel]);
    request.data = state->temporary;
    return request;
}

static void drive(void *instance, const SimState *current, SimCycleDrive *drive_state)
{
    SimDma *dma = instance;
    const SimDmaState *state = state_current(dma, current);
    SimBusRequest request;

    if (dma == NULL || state == NULL || drive_state == NULL ||
        state->active_channel >= SIM_DMA_CHANNEL_COUNT) return;
    switch (state->phase) {
    case SIM_DMA_PHASE_IO_READ:
        request = make_request(dma, state, SIM_BUS_IO, SIM_BUS_READ);
        (void)sim_cycle_drive_add_request(drive_state, &request);
        break;
    case SIM_DMA_PHASE_MEMORY_READ:
    case SIM_DMA_PHASE_VERIFY_READ:
        request = make_request(dma, state, SIM_BUS_MEMORY, SIM_BUS_READ);
        (void)sim_cycle_drive_add_request(drive_state, &request);
        break;
    case SIM_DMA_PHASE_MEMORY_WRITE:
        request = make_request(dma, state, SIM_BUS_MEMORY, SIM_BUS_WRITE);
        (void)sim_cycle_drive_add_request(drive_state, &request);
        break;
    case SIM_DMA_PHASE_IO_WRITE:
        request = make_request(dma, state, SIM_BUS_IO, SIM_BUS_WRITE);
        (void)sim_cycle_drive_add_request(drive_state, &request);
        break;
    case SIM_DMA_PHASE_MEMORY_TO_MEMORY_READ:
        request = make_memory_to_memory_request(dma, state, SIM_BUS_READ);
        (void)sim_cycle_drive_add_request(drive_state, &request);
        break;
    case SIM_DMA_PHASE_MEMORY_TO_MEMORY_WRITE:
        request = make_memory_to_memory_request(dma, state, SIM_BUS_WRITE);
        (void)sim_cycle_drive_add_request(drive_state, &request);
        break;
    default:
        break;
    }
}

static bool response_for_dma(const SimCycleResolution *resolution,
                             SimDmaPhase phase)
{
    (void)phase;
    return resolution != NULL && resolution->bus_response.valid &&
           resolution->bus_response.master_id == SIM_DMA_MASTER_ID &&
           resolution->bus_response.phase == SIM_BUS_T4 &&
           !resolution->bus_response.error && resolution->bus_response.ready;
}

static void advance(SimDma *dma, const SimDmaState *old, SimDmaState *state)
{
    SimDmaChannelState *channel;
    uint8_t bit;
    bool terminal;
    SimDmaServiceMode service;

    channel = &state->channel[old->active_channel];
    bit = (uint8_t)(1u << old->active_channel);
    terminal = old->channel[old->active_channel].current_count == 0u || dma->eop_asserted;
    channel->current_address = (uint16_t)(old->channel[old->active_channel].current_address +
        ((old->channel[old->active_channel].mode & DMA_MODE_DECREMENT) != 0u ? -1 : 1));
    channel->current_count = (uint16_t)(old->channel[old->active_channel].current_count - 1u);
    state->active_channel = 0xFFu;
    state->last_channel = old->active_channel;
    state->phase = SIM_DMA_PHASE_IDLE;
    service = (SimDmaServiceMode)(old->channel[old->active_channel].mode & 0xC0u);
    if (terminal) {
        state->status |= bit;
        state->software_request &= (uint8_t)~bit;
        if ((old->channel[old->active_channel].mode & DMA_MODE_AUTOINIT) != 0u) {
            channel->current_address = old->channel[old->active_channel].base_address;
            channel->current_count = old->channel[old->active_channel].base_count;
        } else {
            state->mask |= bit;
        }
    } else if (service == SIM_DMA_MODE_DEMAND &&
               (dma->dreq_lines & bit) == 0u) {
        state->phase = SIM_DMA_PHASE_IDLE;
    }
}

static void advance_memory_to_memory(SimDma *dma, const SimDmaState *old,
                                     SimDmaState *state)
{
    SimDmaChannelState *source = &state->channel[0];
    SimDmaChannelState *destination = &state->channel[1];
    uint8_t bit = 0x02u;
    bool terminal = old->channel[1].current_count == 0u || dma->eop_asserted;

    if ((old->command & DMA_COMMAND_CHANNEL_0_ADDRESS_HOLD) == 0u) {
        source->current_address = (uint16_t)(old->channel[0].current_address +
            ((old->channel[0].mode & DMA_MODE_DECREMENT) != 0u ? -1 : 1));
    }
    destination->current_address = (uint16_t)(old->channel[1].current_address +
        ((old->channel[1].mode & DMA_MODE_DECREMENT) != 0u ? -1 : 1));
    destination->current_count = (uint16_t)(old->channel[1].current_count - 1u);
    state->active_channel = 0xFFu;
    state->last_channel = 1u;
    state->phase = SIM_DMA_PHASE_IDLE;
    if (!terminal) return;

    state->status |= bit;
    state->software_request &= (uint8_t)~0x01u;
    if ((old->channel[1].mode & DMA_MODE_AUTOINIT) != 0u) {
        source->current_address = old->channel[0].base_address;
        destination->current_address = old->channel[1].base_address;
        destination->current_count = old->channel[1].base_count;
    } else {
        state->mask |= bit;
    }
}

static void sample(void *instance, const SimState *current, SimState *next,
                   const SimCycleResolution *resolution)
{
    SimDma *dma = instance;
    const SimDmaState *old = state_current(dma, current);
    SimDmaState *state;
    int selected;
    SimDmaTransferType type;

    if (dma == NULL || old == NULL || next == NULL) return;
    state = sim_state_region_next(next, dma->state_region);
    if (state == NULL) return;
    if (old->phase == SIM_DMA_PHASE_IDLE) {
        selected = select_channel(dma, old);
        if (selected < 0) return;
        if ((old->command & DMA_COMMAND_MEMORY_TO_MEMORY) != 0u && selected == 0) {
            state->active_channel = 1u;
            state->phase = SIM_DMA_PHASE_MEMORY_TO_MEMORY_READ;
            return;
        }
        if ((old->channel[selected].mode & 0xC0u) == SIM_DMA_MODE_CASCADE) {
            state->active_channel = (uint8_t)selected;
            state->cascade_granted = true;
            state->phase = SIM_DMA_PHASE_CASCADE;
            return;
        }
        if (!dma->binding[selected].connected) return;
        state->active_channel = (uint8_t)selected;
        type = (SimDmaTransferType)(old->channel[selected].mode & 0x0Cu);
        state->phase = type == SIM_DMA_TRANSFER_WRITE ? SIM_DMA_PHASE_IO_READ :
                       type == SIM_DMA_TRANSFER_READ ? SIM_DMA_PHASE_MEMORY_READ :
                       SIM_DMA_PHASE_VERIFY_READ;
        return;
    }
    if (old->active_channel >= SIM_DMA_CHANNEL_COUNT || resolution == NULL ||
        resolution->bus_response.master_id != SIM_DMA_MASTER_ID) {
        if (old->phase == SIM_DMA_PHASE_CASCADE &&
            (dma->dreq_lines & (1u << old->active_channel)) == 0u) {
            state->cascade_granted = false;
            state->active_channel = 0xFFu;
            state->phase = SIM_DMA_PHASE_IDLE;
        }
        return;
    }
    if (resolution->bus_response.phase == SIM_BUS_T1) {
        switch (old->phase) {
        case SIM_DMA_PHASE_IO_READ: state->phase = SIM_DMA_PHASE_WAIT_IO_READ; break;
        case SIM_DMA_PHASE_MEMORY_READ: state->phase = SIM_DMA_PHASE_WAIT_MEMORY_READ; break;
        case SIM_DMA_PHASE_MEMORY_WRITE: state->phase = SIM_DMA_PHASE_WAIT_MEMORY_WRITE; break;
        case SIM_DMA_PHASE_IO_WRITE: state->phase = SIM_DMA_PHASE_WAIT_IO_WRITE; break;
        case SIM_DMA_PHASE_VERIFY_READ: state->phase = SIM_DMA_PHASE_WAIT_VERIFY_READ; break;
        case SIM_DMA_PHASE_MEMORY_TO_MEMORY_READ:
            state->phase = SIM_DMA_PHASE_WAIT_MEMORY_TO_MEMORY_READ;
            break;
        case SIM_DMA_PHASE_MEMORY_TO_MEMORY_WRITE:
            state->phase = SIM_DMA_PHASE_WAIT_MEMORY_TO_MEMORY_WRITE;
            break;
        default: break;
        }
        return;
    }
    if (!response_for_dma(resolution, old->phase)) return;
    switch (old->phase) {
    case SIM_DMA_PHASE_WAIT_IO_READ:
        state->temporary = (uint8_t)resolution->bus_response.data;
        state->phase = SIM_DMA_PHASE_MEMORY_WRITE;
        break;
    case SIM_DMA_PHASE_WAIT_MEMORY_READ:
        state->temporary = (uint8_t)resolution->bus_response.data;
        state->phase = SIM_DMA_PHASE_IO_WRITE;
        break;
    case SIM_DMA_PHASE_WAIT_MEMORY_TO_MEMORY_READ:
        state->temporary = (uint8_t)resolution->bus_response.data;
        state->phase = SIM_DMA_PHASE_MEMORY_TO_MEMORY_WRITE;
        break;
    case SIM_DMA_PHASE_WAIT_MEMORY_WRITE:
    case SIM_DMA_PHASE_WAIT_IO_WRITE:
    case SIM_DMA_PHASE_WAIT_VERIFY_READ:
        advance(dma, old, state);
        break;
    case SIM_DMA_PHASE_WAIT_MEMORY_TO_MEMORY_WRITE:
        advance_memory_to_memory(dma, old, state);
        break;
    default:
        break;
    }
}

static void reset(void *instance, SimState *state_container)
{
    SimDma *dma = instance;
    SimDmaState state = {0};
    if (dma == NULL || state_container == NULL) return;
    state.mask = 0x0Fu;
    state.active_channel = 0xFFu;
    state.last_channel = 3u;
    *(SimDmaState *)sim_state_region_current(state_container, dma->state_region) = state;
    *(SimDmaState *)sim_state_region_next(state_container, dma->state_region) = state;
}

static void evaluate(void *instance, const SimState *current,
                     const SimBusState *transaction, SimBusResponse *response)
{
    SimDma *dma = instance;
    const SimDmaState *state = state_current(dma, current);
    uint16_t port;
    int page;
    unsigned channel;

    if (dma == NULL || state == NULL || transaction == NULL || response == NULL) return;
    response->ready = true;
    if (transaction->request.direction == SIM_BUS_WRITE) return;
    port = (uint16_t)transaction->request.address;
    page = page_channel(port);
    if (page >= 0) {
        response->data = response_byte(&transaction->request, state->channel[page].page);
        return;
    }
    if (port < 8u) {
        channel = port >> 1u;
        response->data = response_byte(&transaction->request,
            (uint8_t)((port & 1u) == 0u
                ? (state->flip_flop_high ? state->channel[channel].current_address >> 8u : state->channel[channel].current_address)
                : (state->flip_flop_high ? state->channel[channel].current_count >> 8u : state->channel[channel].current_count)));
        return;
    }
    if (port == 8u) response->data = response_byte(&transaction->request,
                                                    (uint8_t)((state->status & 0x0Fu) |
                                                              (dma->dreq_lines << 4u)));
    else if (port == 13u) response->data = response_byte(&transaction->request, state->temporary);
    else if (port == 15u) response->data = response_byte(&transaction->request, state->mask);
}

static void write_word_byte(uint16_t *base, uint16_t *current, bool high, uint8_t value)
{
    uint16_t result = high ? (uint16_t)((*base & 0x00FFu) | ((uint16_t)value << 8u))
                           : (uint16_t)((*base & 0xFF00u) | value);
    *base = result;
    *current = result;
}

static void commit(void *instance, const SimState *current, SimState *next,
                   const SimBusRequest *request, const SimBusResponse *response)
{
    SimDma *dma = instance;
    SimDmaState *state;
    uint16_t port;
    uint8_t value;
    int page;
    unsigned channel;

    (void)current;
    if (dma == NULL || next == NULL || request == NULL || response == NULL ||
        !response->ready || response->error) return;
    state = sim_state_region_next(next, dma->state_region);
    if (state == NULL) return;
    port = (uint16_t)request->address;
    if (request->direction == SIM_BUS_READ) {
        if (port < 8u || port == 8u) state->flip_flop_high = !state->flip_flop_high;
        if (port == 8u) state->status &= 0xF0u;
        return;
    }
    value = request_byte(request);
    page = page_channel(port);
    if (page >= 0) { state->channel[page].page = value; return; }
    if (port < 8u) {
        channel = port >> 1u;
        if ((port & 1u) == 0u) write_word_byte(&state->channel[channel].base_address,
                                                &state->channel[channel].current_address,
                                                state->flip_flop_high, value);
        else write_word_byte(&state->channel[channel].base_count,
                             &state->channel[channel].current_count,
                             state->flip_flop_high, value);
        state->flip_flop_high = !state->flip_flop_high;
        return;
    }
    switch (port) {
    case 8u: state->command = value; break;
    case 9u:
        if ((value & 4u) != 0u) state->software_request |= (uint8_t)(1u << (value & 3u));
        else state->software_request &= (uint8_t)~(1u << (value & 3u));
        break;
    case 10u:
        if ((value & 4u) != 0u) state->mask |= (uint8_t)(1u << (value & 3u));
        else state->mask &= (uint8_t)~(1u << (value & 3u));
        break;
    case 11u:
        channel = value & 3u;
        state->channel[channel].mode = value & 0xFCu;
        state->status &= (uint8_t)~(1u << channel);
        break;
    case 12u: state->flip_flop_high = false; break;
    case 13u: {
        SimDmaState reset_state = {0};
        reset_state.mask = 0x0Fu;
        reset_state.active_channel = 0xFFu;
        reset_state.last_channel = 3u;
        *state = reset_state;
        break;
    }
    case 14u: state->mask = 0u; break;
    case 15u: state->mask = value & 0x0Fu; break;
    default: break;
    }
}

void sim_dma_init(SimDma *dma)
{
    if (dma != NULL) memset(dma, 0, sizeof(*dma));
}

void sim_dma_set_dreq(SimDma *dma, unsigned channel, bool asserted)
{
    if (dma == NULL || channel >= SIM_DMA_CHANNEL_COUNT) return;
    if (asserted) dma->dreq_lines |= (uint8_t)(1u << channel);
    else dma->dreq_lines &= (uint8_t)~(1u << channel);
}

void sim_dma_set_eop(SimDma *dma, bool asserted)
{
    if (dma != NULL) dma->eop_asserted = asserted;
}

bool sim_dma_bind_io_port(SimDma *dma, unsigned channel, uint16_t io_port)
{
    if (dma == NULL || channel >= SIM_DMA_CHANNEL_COUNT) return false;
    dma->binding[channel].io_port = io_port;
    dma->binding[channel].connected = true;
    return true;
}

SimBusTarget sim_dma_bus_target(SimDma *dma, const char *name, uint32_t target_id)
{
    SimBusTarget target = {0};
    target.name = name;
    target.target_id = target_id;
    target.instance = dma;
    target.probe = probe;
    target.evaluate = evaluate;
    target.commit = commit;
    return target;
}

bool sim_dma_attach(SimDma *dma, SimKernel *kernel)
{
    SimDmaState reset_state = {0};
    SimModule module = {0};
    if (dma == NULL || kernel == NULL || dma->attached) return false;
    reset_state.mask = 0x0Fu;
    reset_state.active_channel = 0xFFu;
    reset_state.last_channel = 3u;
    if (!sim_state_add_region(kernel->state, "dma8237", sizeof(reset_state),
                              &reset_state, &dma->state_region)) return false;
    dma->attached = true;
    module.name = "dma8237";
    module.instance = dma;
    module.reset = reset;
    module.drive = drive;
    module.sample = sample;
    return sim_kernel_attach_module(kernel, &module);
}

const SimDmaState *sim_dma_current_state(const SimDma *dma, const SimState *state)
{
    return state_current(dma, state);
}
