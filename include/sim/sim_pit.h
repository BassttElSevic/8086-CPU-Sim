#ifndef SIM_SIM_PIT_H
#define SIM_SIM_PIT_H

#include "sim_bus.h"
#include "sim_kernel.h"

typedef struct SimPic SimPic;

typedef struct {
    uint32_t reload;
    uint32_t count;
    uint32_t latch;
    uint8_t status_latch;
    uint8_t mode;
    uint8_t read_write;
    bool bcd;
    bool latch_valid;
    bool status_latch_valid;
    bool write_high_next;
    bool read_high_next;
    bool null_count;
    bool gate;
    bool previous_gate;
    bool out;
    bool running;
} SimPitChannel;

typedef struct {
    SimPitChannel channel[3];
    uint64_t phase;
    bool irq0_pulse;
} SimPitState;

typedef struct {
    uint32_t input_clock_hz;
    uint32_t simulation_clock_hz;
    bool driven_gate[3];
    SimPic *pic;
    SimStateRegionId state_region;
    bool attached;
} SimPit;

void sim_pit_init(SimPit *pit, SimPic *pic);
void sim_pit_reset(SimPit *pit);
void sim_pit_set_clock(SimPit *pit, uint32_t input_hz,
                       uint32_t simulation_hz);
void sim_pit_set_gate(SimPit *pit, unsigned channel, bool asserted);
SimBusTarget sim_pit_bus_target(SimPit *pit, const char *name,
                                uint32_t target_id);
bool sim_pit_attach(SimPit *pit, SimKernel *kernel);
const SimPitState *sim_pit_current_state(const SimPit *pit,
                                         const SimState *state);
bool sim_pit_output(const SimPit *pit, const SimState *state,
                    unsigned channel);

#endif
