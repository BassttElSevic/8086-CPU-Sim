#ifndef SIM_SIM_CGA_H
#define SIM_SIM_CGA_H

#include "sim_bus.h"
#include "sim_kernel.h"

#define SIM_CGA_VRAM_BASE 0xB8000u
#define SIM_CGA_VRAM_SIZE 0x4000u
#define SIM_CGA_IO_BASE 0x3D0u
#define SIM_CGA_IO_END 0x3DFu
#define SIM_CGA_TEXT_COLUMNS 80u
#define SIM_CGA_TEXT_ROWS 25u
#define SIM_CGA_CRTC_REGISTERS 18u
#define SIM_CGA_FRAME_WIDTH 640u
#define SIM_CGA_FRAME_HEIGHT 200u

typedef struct {
    uint8_t vram[SIM_CGA_VRAM_SIZE];
    uint8_t crtc[SIM_CGA_CRTC_REGISTERS];
    uint8_t crtc_index;
    uint8_t mode_control;
    uint8_t color_select;
    bool blink_phase;
    bool dirty;
    uint32_t blink_divider;
    /* CPU-clock ticks within the current CGA video frame. */
    uint32_t frame_tick;
} SimCgaState;

typedef struct {
    SimStateRegionId state_region;
    bool attached;
    bool console_enabled;
    void *console_instance;
    void *console_keyboard;
} SimCga;

void sim_cga_init(SimCga *cga);
SimBusTarget sim_cga_bus_target(SimCga *cga, const char *name, uint32_t target_id);
bool sim_cga_attach(SimCga *cga, SimKernel *kernel);
const SimCgaState *sim_cga_current_state(const SimCga *cga, const SimState *state);
void sim_cga_set_console_enabled(SimCga *cga, bool enabled);

#endif
