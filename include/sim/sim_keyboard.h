#ifndef SIM_SIM_KEYBOARD_H
#define SIM_SIM_KEYBOARD_H

#include "sim_bus.h"
#include "sim_kernel.h"

#define SIM_KEYBOARD_FIFO_CAPACITY 32u
#define SIM_KEYBOARD_INGRESS_CAPACITY 64u

enum {
    SIM_KEYBOARD_PORT_DATA = 0x60u,
    SIM_KEYBOARD_PORT_STATUS_COMMAND = 0x64u,
    SIM_KEYBOARD_STATUS_OUTPUT_FULL = 0x01u,
    SIM_KEYBOARD_STATUS_INPUT_FULL = 0x02u,
    SIM_KEYBOARD_STATUS_SYSTEM = 0x04u,
    SIM_KEYBOARD_STATUS_COMMAND_DATA = 0x08u
};

typedef enum {
    SIM_KEYBOARD_EXPECT_NONE = 0,
    SIM_KEYBOARD_EXPECT_COMMAND_BYTE,
    SIM_KEYBOARD_EXPECT_OUTPUT_PORT,
    SIM_KEYBOARD_EXPECT_LED,
    SIM_KEYBOARD_EXPECT_TYPEMATIC,
    SIM_KEYBOARD_EXPECT_SCANCODE_SET
} SimKeyboardExpectation;

typedef struct {
    uint8_t command_byte;
    uint8_t output_port;
    uint8_t fifo[SIM_KEYBOARD_FIFO_CAPACITY];
    uint8_t fifo_head;
    uint8_t fifo_count;
    SimKeyboardExpectation expectation;
    bool keyboard_interface_enabled;
    bool scanning_enabled;
    bool output_from_controller;
    bool irq1_pulse;
} SimKeyboardState;

typedef struct SimPic SimPic;

typedef struct {
    SimPic *pic;
    uint8_t irq_line;
    uint8_t ingress[SIM_KEYBOARD_INGRESS_CAPACITY];
    uint8_t ingress_head;
    uint8_t ingress_count;
    SimStateRegionId state_region;
    bool attached;
} SimKeyboard;

void sim_keyboard_init(SimKeyboard *keyboard, SimPic *pic);
void sim_keyboard_reset(SimKeyboard *keyboard);

/* Host adapters submit raw PC/AT scan-code set 1 bytes through this boundary. */
bool sim_keyboard_enqueue_scancode(SimKeyboard *keyboard, uint8_t scancode);

SimBusTarget sim_keyboard_bus_target(SimKeyboard *keyboard, const char *name,
                                     uint32_t target_id);
bool sim_keyboard_attach(SimKeyboard *keyboard, SimKernel *kernel);
const SimKeyboardState *sim_keyboard_current_state(const SimKeyboard *keyboard,
                                                    const SimState *state);

#endif
