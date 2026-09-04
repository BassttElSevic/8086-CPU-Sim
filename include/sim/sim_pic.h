#ifndef SIM_SIM_PIC_H
#define SIM_SIM_PIC_H

#include "sim_bus.h"
#include "sim_kernel.h"

typedef struct {
    uint8_t irr;
    uint8_t imr;
    uint8_t isr;
    uint8_t vector_base;
    uint8_t priority_add;
    uint8_t cascade_line;
    uint8_t read_select_isr;
    uint8_t icw_step;
    uint8_t icw3;
    bool expect_icw4;
    bool single;
    bool auto_eoi;
    bool rotate_auto_eoi;
    bool buffered;
    bool special_fully_nested;
    bool level_triggered;
    bool special_mask_mode;
    bool initialized;
    bool poll;
    uint8_t pending_inta_level;
    uint8_t inta_phase;
    uint8_t input_lines;
    uint8_t previous_input_lines;
} SimPicState;

typedef struct SimPic {
    uint16_t command_port;
    uint16_t data_port;
    uint8_t driven_irq_lines;
    struct SimPic *slave;
    uint8_t cascade_line;
    bool cascaded;
    SimStateRegionId state_region;
    bool attached;
} SimPic;

void sim_pic_init(SimPic *pic);
void sim_pic_reset(SimPic *pic);
void sim_pic_set_irq_lines(SimPic *pic, uint8_t lines);
void sim_pic_set_irq_line(SimPic *pic, unsigned line, bool asserted);
void sim_pic_pulse_irq(SimPic *pic, unsigned line);
void sim_pic_set_ports(SimPic *pic, uint16_t command_port, uint16_t data_port);
void sim_pic_connect_slave(SimPic *master, SimPic *slave, unsigned irq_line);

SimBusTarget sim_pic_bus_target(SimPic *pic, const char *name,
                                uint32_t target_id);
bool sim_pic_attach(SimPic *pic, SimKernel *kernel);

uint8_t sim_pic_pending(const SimPic *pic, const SimState *state);
const SimPicState *sim_pic_current_state(const SimPic *pic,
                                         const SimState *state);

#endif
