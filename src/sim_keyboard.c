#include "sim/sim_keyboard.h"

#include <string.h>

#include "sim/sim_pic.h"
#include "sim/sim_state.h"

enum {
    KBD_COMMAND_READ_COMMAND_BYTE = 0x20u,
    KBD_COMMAND_WRITE_COMMAND_BYTE = 0x60u,
    KBD_COMMAND_SELF_TEST = 0xAAu,
    KBD_COMMAND_INTERFACE_TEST = 0xABu,
    KBD_COMMAND_DISABLE_INTERFACE = 0xADu,
    KBD_COMMAND_ENABLE_INTERFACE = 0xAEu,
    KBD_COMMAND_READ_OUTPUT_PORT = 0xD0u,
    KBD_COMMAND_WRITE_OUTPUT_PORT = 0xD1u,
    KBD_REPLY_ACK = 0xFAu,
    KBD_REPLY_SELF_TEST = 0xAAu,
    KBD_REPLY_CONTROLLER_OK = 0x55u
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

static bool fifo_push(SimKeyboardState *state, uint8_t value)
{
    uint8_t index;
    if (state == NULL || state->fifo_count >= SIM_KEYBOARD_FIFO_CAPACITY) return false;
    index = (uint8_t)((state->fifo_head + state->fifo_count) % SIM_KEYBOARD_FIFO_CAPACITY);
    state->fifo[index] = value;
    state->fifo_count++;
    return true;
}

static uint8_t fifo_peek(const SimKeyboardState *state)
{
    return state == NULL || state->fifo_count == 0u ? 0u : state->fifo[state->fifo_head];
}

static void fifo_pop(SimKeyboardState *state)
{
    if (state == NULL || state->fifo_count == 0u) return;
    state->fifo_head = (uint8_t)((state->fifo_head + 1u) % SIM_KEYBOARD_FIFO_CAPACITY);
    state->fifo_count--;
}

static void raise_irq1(SimKeyboard *keyboard, SimKeyboardState *state)
{
    if (keyboard == NULL || state == NULL ||
        (state->command_byte & 0x01u) == 0u || state->fifo_count == 0u) return;
    if (keyboard->pic != NULL) sim_pic_pulse_irq(keyboard->pic, keyboard->irq_line);
    state->irq1_pulse = true;
}

static uint8_t status_byte(const SimKeyboardState *state)
{
    uint8_t value = SIM_KEYBOARD_STATUS_SYSTEM;
    if (state == NULL) return value;
    if (state->fifo_count != 0u) value |= SIM_KEYBOARD_STATUS_OUTPUT_FULL;
    if (state->output_from_controller) value |= SIM_KEYBOARD_STATUS_COMMAND_DATA;
    return value;
}

static void queue_controller_reply(SimKeyboard *keyboard, SimKeyboardState *state,
                                   uint8_t value)
{
    if (fifo_push(state, value)) {
        state->output_from_controller = true;
        raise_irq1(keyboard, state);
    }
}

static void receive_keyboard_data(SimKeyboard *keyboard, SimKeyboardState *state,
                                  uint8_t value)
{
    if (state->expectation != SIM_KEYBOARD_EXPECT_NONE) {
        state->expectation = SIM_KEYBOARD_EXPECT_NONE;
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        return;
    }

    switch (value) {
    case 0xEDu:
        state->expectation = SIM_KEYBOARD_EXPECT_LED;
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        break;
    case 0xEEu:
        queue_controller_reply(keyboard, state, 0xEEu);
        break;
    case 0xF0u:
        state->expectation = SIM_KEYBOARD_EXPECT_SCANCODE_SET;
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        break;
    case 0xF2u:
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        queue_controller_reply(keyboard, state, 0xABu);
        queue_controller_reply(keyboard, state, 0x83u);
        break;
    case 0xF3u:
        state->expectation = SIM_KEYBOARD_EXPECT_TYPEMATIC;
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        break;
    case 0xF4u:
        state->scanning_enabled = true;
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        break;
    case 0xF5u:
        state->scanning_enabled = false;
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        break;
    case 0xFFu:
        state->scanning_enabled = true;
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        queue_controller_reply(keyboard, state, KBD_REPLY_SELF_TEST);
        break;
    default:
        queue_controller_reply(keyboard, state, KBD_REPLY_ACK);
        break;
    }
}

static bool keyboard_probe(void *instance, const SimBusRequest *request)
{
    (void)instance;
    return request != NULL && request->valid && request->kind == SIM_BUS_IO &&
           (request->address == SIM_KEYBOARD_PORT_DATA ||
            request->address == SIM_KEYBOARD_PORT_STATUS_COMMAND);
}

static void keyboard_evaluate(void *instance, const SimState *current,
                              const SimBusState *transaction, SimBusResponse *response)
{
    SimKeyboard *keyboard = instance;
    const SimKeyboardState *state = sim_keyboard_current_state(keyboard, current);
    if (state == NULL || transaction == NULL || response == NULL) return;
    response->ready = true;
    if (transaction->request.direction == SIM_BUS_WRITE) return;
    if (transaction->request.address == SIM_KEYBOARD_PORT_DATA) {
        response->data = response_byte(&transaction->request, fifo_peek(state));
    } else {
        response->data = response_byte(&transaction->request, status_byte(state));
    }
}

static void receive_controller_command(SimKeyboard *keyboard, SimKeyboardState *state,
                                       uint8_t value)
{
    switch (value) {
    case KBD_COMMAND_READ_COMMAND_BYTE:
        queue_controller_reply(keyboard, state, state->command_byte);
        break;
    case KBD_COMMAND_WRITE_COMMAND_BYTE:
        state->expectation = SIM_KEYBOARD_EXPECT_COMMAND_BYTE;
        break;
    case KBD_COMMAND_SELF_TEST:
        queue_controller_reply(keyboard, state, KBD_REPLY_CONTROLLER_OK);
        break;
    case KBD_COMMAND_INTERFACE_TEST:
        queue_controller_reply(keyboard, state, 0x00u);
        break;
    case KBD_COMMAND_DISABLE_INTERFACE:
        state->keyboard_interface_enabled = false;
        break;
    case KBD_COMMAND_ENABLE_INTERFACE:
        state->keyboard_interface_enabled = true;
        break;
    case KBD_COMMAND_READ_OUTPUT_PORT:
        queue_controller_reply(keyboard, state, state->output_port);
        break;
    case KBD_COMMAND_WRITE_OUTPUT_PORT:
        state->expectation = SIM_KEYBOARD_EXPECT_OUTPUT_PORT;
        break;
    default:
        break;
    }
}

static void keyboard_commit(void *instance, const SimState *current, SimState *next,
                            const SimBusRequest *request, const SimBusResponse *response)
{
    SimKeyboard *keyboard = instance;
    SimKeyboardState *state;
    uint8_t value;
    (void)current;
    if (keyboard == NULL || next == NULL || request == NULL || response == NULL ||
        !response->ready || response->error) return;
    state = (SimKeyboardState *)sim_state_region_next(next, keyboard->state_region);
    if (state == NULL) return;
    if (request->direction == SIM_BUS_READ) {
        if (request->address == SIM_KEYBOARD_PORT_DATA && state->fifo_count != 0u) {
            fifo_pop(state);
            state->output_from_controller = false;
            if (state->fifo_count != 0u) raise_irq1(keyboard, state);
        }
        return;
    }
    value = request_byte(request);
    if (request->address == SIM_KEYBOARD_PORT_STATUS_COMMAND) {
        receive_controller_command(keyboard, state, value);
        return;
    }
    if (state->expectation == SIM_KEYBOARD_EXPECT_COMMAND_BYTE) {
        state->command_byte = value;
        state->expectation = SIM_KEYBOARD_EXPECT_NONE;
        if (state->fifo_count != 0u) raise_irq1(keyboard, state);
    } else if (state->expectation == SIM_KEYBOARD_EXPECT_OUTPUT_PORT) {
        state->output_port = value;
        state->expectation = SIM_KEYBOARD_EXPECT_NONE;
    } else {
        receive_keyboard_data(keyboard, state, value);
    }
}

static void keyboard_reset(void *instance, SimState *state)
{
    SimKeyboard *keyboard = instance;
    SimKeyboardState reset = {0};
    if (keyboard == NULL || state == NULL) return;
    sim_keyboard_reset(keyboard);
    reset.command_byte = 0x01u;
    reset.output_port = 0x01u;
    reset.keyboard_interface_enabled = true;
    reset.scanning_enabled = true;
    *(SimKeyboardState *)sim_state_region_current(state, keyboard->state_region) = reset;
    *(SimKeyboardState *)sim_state_region_next(state, keyboard->state_region) = reset;
}

static void keyboard_sample(void *instance, const SimState *current, SimState *next,
                            const SimCycleResolution *resolution)
{
    SimKeyboard *keyboard = instance;
    const SimKeyboardState *old = sim_keyboard_current_state(keyboard, current);
    SimKeyboardState *state;
    uint8_t value;
    (void)resolution;
    if (keyboard == NULL || old == NULL || next == NULL) return;
    state = (SimKeyboardState *)sim_state_region_next(next, keyboard->state_region);
    if (state == NULL) return;
    if (old->irq1_pulse) {
        if (keyboard->pic != NULL) sim_pic_set_irq_line(keyboard->pic, keyboard->irq_line, false);
        state->irq1_pulse = false;
    }
    if (keyboard->ingress_count == 0u || !old->keyboard_interface_enabled || !old->scanning_enabled) return;
    value = keyboard->ingress[keyboard->ingress_head];
    if (!fifo_push(state, value)) return;
    keyboard->ingress_head = (uint8_t)((keyboard->ingress_head + 1u) % SIM_KEYBOARD_INGRESS_CAPACITY);
    keyboard->ingress_count--;
    state->output_from_controller = false;
    raise_irq1(keyboard, state);
}

void sim_keyboard_init(SimKeyboard *keyboard, SimPic *pic)
{
    if (keyboard == NULL) return;
    memset(keyboard, 0, sizeof(*keyboard));
    keyboard->pic = pic;
    keyboard->irq_line = 1u;
}

void sim_keyboard_reset(SimKeyboard *keyboard)
{
    if (keyboard == NULL) return;
    keyboard->ingress_head = 0u;
    keyboard->ingress_count = 0u;
}

bool sim_keyboard_enqueue_scancode(SimKeyboard *keyboard, uint8_t scancode)
{
    uint8_t index;
    if (keyboard == NULL || keyboard->ingress_count >= SIM_KEYBOARD_INGRESS_CAPACITY) return false;
    index = (uint8_t)((keyboard->ingress_head + keyboard->ingress_count) % SIM_KEYBOARD_INGRESS_CAPACITY);
    keyboard->ingress[index] = scancode;
    keyboard->ingress_count++;
    return true;
}

SimBusTarget sim_keyboard_bus_target(SimKeyboard *keyboard, const char *name,
                                     uint32_t target_id)
{
    SimBusTarget target = {0};
    target.name = name;
    target.target_id = target_id;
    target.instance = keyboard;
    target.probe = keyboard_probe;
    target.evaluate = keyboard_evaluate;
    target.commit = keyboard_commit;
    return target;
}

bool sim_keyboard_attach(SimKeyboard *keyboard, SimKernel *kernel)
{
    SimKeyboardState reset = {0};
    SimModule module = {0};
    if (keyboard == NULL || kernel == NULL || kernel->state == NULL || keyboard->attached) return false;
    reset.command_byte = 0x01u;
    reset.output_port = 0x01u;
    reset.keyboard_interface_enabled = true;
    reset.scanning_enabled = true;
    if (!sim_state_add_region(kernel->state, "keyboard", sizeof(reset), &reset,
                              &keyboard->state_region)) return false;
    keyboard->attached = true;
    module.name = "keyboard";
    module.instance = keyboard;
    module.reset = keyboard_reset;
    module.sample = keyboard_sample;
    keyboard->attached = sim_kernel_attach_module(kernel, &module);
    return keyboard->attached;
}

const SimKeyboardState *sim_keyboard_current_state(const SimKeyboard *keyboard,
                                                    const SimState *state)
{
    if (keyboard == NULL || state == NULL || !keyboard->attached) return NULL;
    return (const SimKeyboardState *)sim_state_region_current_const(state,
                                                                      keyboard->state_region);
}
