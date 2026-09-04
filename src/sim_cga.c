#include "sim/sim_cga.h"

#include <string.h>

#include "sim/sim_cga_console.h"
#include "sim/sim_state.h"

enum {
    CGA_FRAME_TICKS = 79545u,
    CGA_RETRACE_START = 74200u,
    CGA_BLINK_HALF_PERIOD_TICKS = 2386364u
};

static const uint8_t crtc_write_masks[SIM_CGA_CRTC_REGISTERS] = {
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x7Fu, 0x1Fu,
    0x7Fu, 0x7Fu, 0xF3u, 0x1Fu, 0x7Fu, 0x1Fu,
    0x3Fu, 0xFFu, 0x3Fu, 0xFFu, 0x00u, 0x00u
};

static bool cga_memory(const SimBusRequest *request)
{
    return request->kind == SIM_BUS_MEMORY &&
           request->address >= SIM_CGA_VRAM_BASE &&
           request->address < SIM_CGA_VRAM_BASE + SIM_CGA_VRAM_SIZE;
}

static uint32_t cga_vram_offset(uint32_t address)
{
    return address - SIM_CGA_VRAM_BASE;
}

static bool cga_io(const SimBusRequest *request)
{
    return request->kind == SIM_BUS_IO &&
           request->address >= SIM_CGA_IO_BASE && request->address <= SIM_CGA_IO_END;
}

static uint32_t cga_io_register(uint32_t address)
{
    uint32_t offset = address - SIM_CGA_IO_BASE;

    /* IBM's decode exposes the 6845 pair through the low address aliases. */
    if (offset <= 7u) return offset & 1u ? 0x3D5u : 0x3D4u;
    return address;
}
static uint8_t request_byte(const SimBusRequest *request) { return request->byte_enable == SIM_BUS_BYTE_ENABLE_HIGH ? (uint8_t)(request->data >> 8u) : (uint8_t)request->data; }
static uint16_t response_byte(const SimBusRequest *request, uint8_t value) { return request->byte_enable == SIM_BUS_BYTE_ENABLE_HIGH ? (uint16_t)((uint16_t)value << 8u) : value; }
const SimCgaState *sim_cga_current_state(const SimCga *cga, const SimState *state) { return cga != NULL && cga->attached ? (const SimCgaState *)sim_state_region_current_const(state, cga->state_region) : NULL; }
void sim_cga_init(SimCga *cga) { if (cga != NULL) memset(cga, 0, sizeof(*cga)); }
void sim_cga_set_console_enabled(SimCga *cga, bool enabled) { if (cga != NULL) cga->console_enabled = enabled; }
static bool probe(void *instance, const SimBusRequest *request) { return instance != NULL && request != NULL && request->valid && (cga_memory(request) || cga_io(request)); }
static uint8_t status_register(const SimCgaState *state)
{
    uint8_t status = 0u;

    if (state->frame_tick >= CGA_RETRACE_START) status |= 0x08u;
    /* Bit 0 is display-enable, low during horizontal and vertical blanking. */
    if (state->frame_tick < CGA_RETRACE_START && (state->frame_tick % 228u) < 180u) {
        status |= 0x01u;
    }
    return status;
}

static void evaluate(void *instance, const SimState *current,
                     const SimBusState *transaction, SimBusResponse *response)
{
    SimCga *cga = instance;
    const SimCgaState *state = sim_cga_current_state(cga, current);
    const SimBusRequest *request;
    uint32_t offset;

    if (state == NULL || transaction == NULL || response == NULL) return;
    request = &transaction->request;
    response->ready = true;
    if (request->direction == SIM_BUS_WRITE) return;
    if (cga_memory(request)) {
        offset = cga_vram_offset(request->address);
        if (request->byte_enable == (SIM_BUS_BYTE_ENABLE_LOW | SIM_BUS_BYTE_ENABLE_HIGH) &&
            offset + 1u < SIM_CGA_VRAM_SIZE) {
            response->data = (uint16_t)(state->vram[offset] |
                                        ((uint16_t)state->vram[offset + 1u] << 8u));
        } else {
            response->data = response_byte(request, state->vram[offset]);
        }
        return;
    }
    if (cga_io_register(request->address) == 0x3D5u && state->crtc_index < SIM_CGA_CRTC_REGISTERS) {
        response->data = response_byte(request, state->crtc[state->crtc_index]);
    } else if (cga_io_register(request->address) == 0x3DAu) {
        response->data = response_byte(request, status_register(state));
    } else {
        response->data = 0u;
    }
}

static void commit(void *instance, const SimState *current, SimState *next,
                   const SimBusRequest *request, const SimBusResponse *response)
{
    SimCga *cga = instance;
    SimCgaState *state;
    uint32_t offset;
    uint8_t value;

    (void)current;
    (void)response;
    if (cga == NULL || request == NULL || request->direction != SIM_BUS_WRITE) return;
    state = (SimCgaState *)sim_state_region_next(next, cga->state_region);
    if (state == NULL) return;
    if (cga_memory(request)) {
        offset = cga_vram_offset(request->address);
        if (request->width == SIM_BUS_BYTE) {
            state->vram[offset] = request_byte(request);
        } else {
            if ((request->byte_enable & SIM_BUS_BYTE_ENABLE_LOW) != 0u) state->vram[offset] = (uint8_t)request->data;
            if ((request->byte_enable & SIM_BUS_BYTE_ENABLE_HIGH) != 0u && offset + 1u < SIM_CGA_VRAM_SIZE) state->vram[offset + 1u] = (uint8_t)(request->data >> 8u);
        }
        state->dirty = true;
        return;
    }
    value = request_byte(request);
    switch (cga_io_register(request->address)) {
    case 0x3D4u:
        state->crtc_index = value & 0x1Fu;
        break;
    case 0x3D5u:
        if (state->crtc_index < SIM_CGA_CRTC_REGISTERS) {
            state->crtc[state->crtc_index] = value & crtc_write_masks[state->crtc_index];
            state->dirty = true;
        }
        break;
    case 0x3D8u:
        state->mode_control = value;
        state->dirty = true;
        break;
    case 0x3D9u:
        state->color_select = value;
        state->dirty = true;
        break;
    default:
        break;
    }
}

static void sample(void *instance, const SimState *current, SimState *next,
                   const SimCycleResolution *resolution)
{
    SimCga *cga = instance;
    const SimCgaState *old = sim_cga_current_state(cga, current);
    SimCgaState *state;

    (void)resolution;
    if (cga == NULL || old == NULL) return;
    state = (SimCgaState *)sim_state_region_next(next, cga->state_region);
    state->frame_tick = (old->frame_tick + 1u) % CGA_FRAME_TICKS;
    if (++state->blink_divider >= CGA_BLINK_HALF_PERIOD_TICKS) {
        state->blink_divider = 0u;
        state->blink_phase = !old->blink_phase;
        state->dirty = true;
    }
    /* The host observer refreshes the completed frame at display cadence. */
}
static void initialize_text_crtc(SimCgaState *state)
{
    static const uint8_t text_80x25[SIM_CGA_CRTC_REGISTERS] = {
        0x71u, 0x50u, 0x5Au, 0x0Au, 0x1Fu, 0x06u,
        0x19u, 0x1Cu, 0x02u, 0x07u, 0x06u, 0x07u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u
    };
    memcpy(state->crtc, text_80x25, sizeof(text_80x25));
    state->mode_control = 0x29u;
    state->dirty = true;
}

static void reset(void *instance, SimState *state) { SimCga *cga = instance; SimCgaState value = {0}; if (cga == NULL) return; initialize_text_crtc(&value); *(SimCgaState *)sim_state_region_current(state, cga->state_region) = value; *(SimCgaState *)sim_state_region_next(state, cga->state_region) = value; }
static void destroy(void *instance) { sim_cga_console_destroy((SimCga *)instance); }
SimBusTarget sim_cga_bus_target(SimCga *cga, const char *name, uint32_t target_id) { SimBusTarget target = {0}; target.name = name; target.target_id = target_id; target.instance = cga; target.probe = probe; target.evaluate = evaluate; target.commit = commit; return target; }
bool sim_cga_attach(SimCga *cga, SimKernel *kernel) { SimCgaState reset_value = {0}; SimModule module = {0}; if (cga == NULL || kernel == NULL || cga->attached) return false; initialize_text_crtc(&reset_value); if (!sim_state_add_region(kernel->state, "cga", sizeof(reset_value), &reset_value, &cga->state_region)) return false; cga->attached = true; module.name = "cga"; module.instance = cga; module.reset = reset; module.sample = sample; module.destroy = destroy; return sim_kernel_attach_module(kernel, &module); }
