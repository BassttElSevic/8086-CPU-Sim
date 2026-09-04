#include "sim/cpu8086_eu.h"

#include <string.h>

#include "sim/cpu8086.h"
#include "sim/cpu8086_alu.h"
#include "sim/cpu8086_bcd.h"
#include "sim/cpu8086_biu.h"
#include "sim/cpu8086_decoder.h"
#include "sim/cpu8086_ea.h"
#include "sim/cpu8086_muldiv.h"
#include "sim/cpu8086_prefetch.h"
#include "sim/cpu8086_shifter.h"

enum {
    CPU8086_STRING_STEP_START = 0u,
    CPU8086_STRING_STEP_SECOND = 1u,
    CPU8086_STRING_STEP_FINISH = 2u,
    CPU8086_STRING_STEP_REPEAT_BOUNDARY = 3u
};

static uint16_t immediate_value(const Cpu8086EuState *eu)
{
    uint8_t start;
    uint16_t value;

    if (eu == NULL || eu->immediate_bytes == 0u ||
        eu->operand_needed < eu->immediate_bytes) {
        return 0u;
    }

    start = (uint8_t)(eu->operand_needed - eu->immediate_bytes);
    value = eu->operands[start];

    if (eu->immediate_bytes >= 2u) {
        value |= (uint16_t)((uint16_t)eu->operands[start + 1u] << 8u);
    }
    return value;
}

static uint8_t instruction_width(const Cpu8086EuState *eu)
{
    return eu != NULL && eu->operand_width_8 ? 8u : 16u;
}

static bool instruction_is_external_interrupt(Cpu8086Instruction instruction)
{
    return instruction == CPU8086_INSTRUCTION_EXTERNAL_NMI ||
           instruction == CPU8086_INSTRUCTION_EXTERNAL_INTR;
}

static bool instruction_is_interrupt_entry(Cpu8086Instruction instruction)
{
    return instruction == CPU8086_INSTRUCTION_DIVIDE_ERROR ||
           instruction == CPU8086_INSTRUCTION_SINGLE_STEP ||
           instruction_is_external_interrupt(instruction);
}

static bool instruction_updates_trap_flag(Cpu8086Instruction instruction)
{
    /* POPF/IRET make their restored TF effective only for the next instruction. */
    return instruction == CPU8086_INSTRUCTION_POPF ||
           instruction == CPU8086_INSTRUCTION_IRET;
}

static bool instruction_is_string(Cpu8086Instruction instruction)
{
    return instruction == CPU8086_INSTRUCTION_STRING;
}

static void retire_interrupt_shadow(Cpu8086State *state)
{
    if (state != NULL && state->eu.interrupt_shadow &&
        state->segment[CPU8086_SEG_CS] == state->eu.interrupt_shadow_cs &&
        state->eu.instruction_start_ip == state->eu.interrupt_shadow_ip) {
        state->eu.interrupt_shadow = false;
        state->eu.interrupt_shadow_cs = 0u;
        state->eu.interrupt_shadow_ip = 0u;
    }
}

static void arm_interrupt_shadow(Cpu8086State *state)
{
    if (state != NULL) {
        state->eu.interrupt_shadow = true;
        state->eu.interrupt_shadow_cs = state->segment[CPU8086_SEG_CS];
        state->eu.interrupt_shadow_ip = state->ip;
    }
}

static void finish_instruction(Cpu8086State *state)
{
    if (state == NULL) {
        return;
    }

    if (!instruction_is_interrupt_entry(state->eu.instruction)) {
        retire_interrupt_shadow(state);
    }
    state->eu.phase = CPU8086_EU_NEED_OPCODE;
    state->eu.instruction = CPU8086_INSTRUCTION_UNSUPPORTED;
    state->eu.operand_count = 0u;
    state->eu.operand_needed = 0u;
    state->eu.immediate_bytes = 0u;
    state->eu.displacement_bytes = 0u;
    state->eu.micro_step = 0u;
    state->eu.needs_modrm = false;
    state->eu.operand_width_8 = false;
    state->eu.direction_to_reg = false;
    state->eu.immediate_sign_extend = false;
    state->eu.modrm = 0u;
    state->eu.segment_override = false;
    state->eu.prefix_seen = false;
    state->eu.lock_prefix = false;
    state->eu.override_segment = CPU8086_SEG_DS;
    state->eu.repeat_prefix = CPU8086_REPEAT_NONE;
    memset(state->eu.temporary, 0, sizeof(state->eu.temporary));
}

static bool decode_prefix(uint8_t byte, Cpu8086EuState *eu)
{
    if (eu == NULL) {
        return false;
    }

    switch (byte) {
    case 0x26u:
        eu->segment_override = true;
        eu->override_segment = CPU8086_SEG_ES;
        return true;
    case 0x2Eu:
        eu->segment_override = true;
        eu->override_segment = CPU8086_SEG_CS;
        return true;
    case 0x36u:
        eu->segment_override = true;
        eu->override_segment = CPU8086_SEG_SS;
        return true;
    case 0x3Eu:
        eu->segment_override = true;
        eu->override_segment = CPU8086_SEG_DS;
        return true;
    case 0xF0u:
    case 0xF1u: /* 8086/8088 silicon treats F1 as a LOCK prefix alias. */
        eu->lock_prefix = true;
        return true;
    case 0xF2u:
        eu->repeat_prefix = CPU8086_REPEAT_WHILE_NOT_EQUAL;
        return true;
    case 0xF3u:
        eu->repeat_prefix = CPU8086_REPEAT_WHILE_EQUAL;
        return true;
    default:
        return false;
    }
}

static uint8_t modrm_displacement_bytes(uint8_t byte)
{
    Cpu8086ModRm modrm = cpu8086_decode_modrm(byte);

    if (modrm.mod == 0u && modrm.rm == 6u) {
        return 2u;
    }
    if (modrm.mod == 1u) {
        return 1u;
    }
    return modrm.mod == 2u ? 2u : 0u;
}

static bool modrm_is_register(const Cpu8086EuState *eu)
{
    return eu != NULL && cpu8086_decode_modrm(eu->modrm).mod == 3u;
}

static int16_t modrm_displacement(const Cpu8086EuState *eu)
{
    uint16_t value;

    if (eu == NULL || eu->displacement_bytes == 0u) {
        return 0;
    }

    value = eu->operands[1u];
    if (eu->displacement_bytes == 1u) {
        return (int16_t)(int8_t)value;
    }

    return (int16_t)(value | ((uint16_t)eu->operands[2u] << 8u));
}

static bool rm_memory_address(Cpu8086State *state, uint32_t *physical_address)
{
    Cpu8086EffectiveAddress address;

    if (state == NULL || physical_address == NULL || modrm_is_register(&state->eu) ||
        !cpu8086_effective_address(state,
                                   cpu8086_decode_modrm(state->eu.modrm),
                                   modrm_displacement(&state->eu),
                                   &address)) {
        if (state != NULL) {
            state->eu.phase = CPU8086_EU_FAULTED;
            state->eu.fault = CPU8086_FAULT_UNSUPPORTED_ADDRESSING;
        }
        return false;
    }

    *physical_address = cpu8086_effective_physical_address(state,
                                                             &address,
                                                             state->eu.segment_override,
                                                             state->eu.override_segment);
    return true;
}

static bool esc_memory_address(const Cpu8086State *state,
                               uint32_t *physical_address)
{
    Cpu8086EffectiveAddress address;

    if (state == NULL || physical_address == NULL) {
        return false;
    }

    if (!cpu8086_effective_address(state,
                                   cpu8086_decode_modrm(state->eu.modrm),
                                   modrm_displacement(&state->eu),
                                   &address)) {
        return false;
    }
    if (address.is_register) {
        *physical_address = 0u;
        return true;
    }

    *physical_address = cpu8086_effective_physical_address(state,
                                                             &address,
                                                             state->eu.segment_override,
                                                             state->eu.override_segment);
    return true;
}

static bool take_data_response(Cpu8086State *state, uint16_t *value)
{
    bool error;

    if (!cpu8086_biu_take_data(state, value, &error)) {
        return false;
    }
    if (error) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_BUS_ERROR;
        return false;
    }
    return true;
}

static bool memory_read_with_lock(Cpu8086State *state,
                                  uint32_t physical_address,
                                  uint8_t width_bytes,
                                  uint16_t *value,
                                  SimBusLockAction lock_action)
{
    if (state == NULL || value == NULL) {
        return false;
    }

    if (state->biu.data.phase == CPU8086_BIU_TRANSFER_COMPLETE) {
        return take_data_response(state, value);
    }
    if (cpu8086_biu_data_busy(state)) {
        return false;
    }
    if (!cpu8086_biu_begin_data(state,
                                 CPU8086_BIU_MEMORY,
                                 false,
                                 physical_address,
                                 0u,
                                 width_bytes,
                                 lock_action)) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_BUS_ERROR;
    }
    return false;
}

static bool memory_read(Cpu8086State *state,
                        uint32_t physical_address,
                        uint8_t width_bytes,
                        uint16_t *value)
{
    return memory_read_with_lock(state,
                                 physical_address,
                                 width_bytes,
                                 value,
                                 SIM_BUS_LOCK_NONE);
}

static bool memory_write(Cpu8086State *state,
                         uint32_t physical_address,
                         uint8_t width_bytes,
                         uint16_t value,
                         SimBusLockAction lock_action)
{
    uint16_t ignored;

    if (state == NULL) {
        return false;
    }

    if (state->biu.data.phase == CPU8086_BIU_TRANSFER_COMPLETE) {
        return take_data_response(state, &ignored);
    }
    if (cpu8086_biu_data_busy(state)) {
        return false;
    }
    if (!cpu8086_biu_begin_data(state,
                                 CPU8086_BIU_MEMORY,
                                 true,
                                 physical_address,
                                 value,
                                 width_bytes,
                                 lock_action)) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_BUS_ERROR;
    }
    return false;
}

static bool io_read(Cpu8086State *state,
                    uint16_t port,
                    uint8_t width_bytes,
                    uint16_t *value)
{
    if (state == NULL || value == NULL) {
        return false;
    }

    if (state->biu.data.phase == CPU8086_BIU_TRANSFER_COMPLETE) {
        return take_data_response(state, value);
    }
    if (cpu8086_biu_data_busy(state)) {
        return false;
    }
    if (!cpu8086_biu_begin_data(state,
                                 CPU8086_BIU_IO,
                                 false,
                                 port,
                                 0u,
                                 width_bytes,
                                 SIM_BUS_LOCK_NONE)) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_BUS_ERROR;
    }
    return false;
}

static bool io_write(Cpu8086State *state,
                     uint16_t port,
                     uint8_t width_bytes,
                     uint16_t value)
{
    uint16_t ignored;

    if (state == NULL) {
        return false;
    }

    if (state->biu.data.phase == CPU8086_BIU_TRANSFER_COMPLETE) {
        return take_data_response(state, &ignored);
    }
    if (cpu8086_biu_data_busy(state)) {
        return false;
    }
    if (!cpu8086_biu_begin_data(state,
                                 CPU8086_BIU_IO,
                                 true,
                                 port,
                                 value,
                                 width_bytes,
                                 SIM_BUS_LOCK_NONE)) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_BUS_ERROR;
    }
    return false;
}

static bool interrupt_acknowledge(Cpu8086State *state, uint16_t *value)
{
    if (state == NULL || value == NULL) {
        return false;
    }

    if (state->biu.data.phase == CPU8086_BIU_TRANSFER_COMPLETE) {
        return take_data_response(state, value);
    }
    if (cpu8086_biu_data_busy(state)) {
        return false;
    }
    if (!cpu8086_biu_begin_data(state,
                                 CPU8086_BIU_INTERRUPT_ACK,
                                 false,
                                 0u,
                                 0u,
                                 1u,
                                 SIM_BUS_LOCK_NONE)) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_BUS_ERROR;
    }
    return false;
}

static bool string_opcode_is_comparison(uint8_t opcode)
{
    return opcode == 0xA6u || opcode == 0xA7u ||
           opcode == 0xAEu || opcode == 0xAFu;
}

static uint8_t string_width_bytes(const Cpu8086EuState *eu)
{
    return eu != NULL && eu->operand_width_8 ? 1u : 2u;
}

static uint32_t string_source_physical_address(const Cpu8086State *state)
{
    Cpu8086Segment segment;

    if (state == NULL) {
        return 0u;
    }

    segment = state->eu.segment_override
        ? state->eu.override_segment
        : CPU8086_SEG_DS;
    return cpu8086_physical_address(state->segment[segment],
                                    state->general[CPU8086_REG_SI]);
}

static uint32_t string_destination_physical_address(const Cpu8086State *state)
{
    if (state == NULL) {
        return 0u;
    }

    return cpu8086_physical_address(state->segment[CPU8086_SEG_ES],
                                    state->general[CPU8086_REG_DI]);
}

static void advance_string_index(Cpu8086State *state,
                                 Cpu8086Register register_id,
                                 uint8_t width_bytes)
{
    if (state == NULL || register_id >= CPU8086_REG_COUNT) {
        return;
    }

    if ((state->flags & CPU8086_FLAG_DIRECTION) != 0u) {
        state->general[register_id] =
            (uint16_t)(state->general[register_id] - width_bytes);
    } else {
        state->general[register_id] =
            (uint16_t)(state->general[register_id] + width_bytes);
    }
}

static bool string_repeat_continues(const Cpu8086State *state)
{
    bool zero;

    if (state == NULL || state->eu.repeat_prefix == CPU8086_REPEAT_NONE ||
        state->general[CPU8086_REG_CX] == 0u) {
        return false;
    }

    if (!string_opcode_is_comparison(state->eu.opcode)) {
        return true;
    }

    zero = (state->flags & CPU8086_FLAG_ZERO) != 0u;
    return state->eu.repeat_prefix == CPU8086_REPEAT_WHILE_EQUAL
        ? zero
        : !zero;
}

static void finish_string_iteration(Cpu8086State *state)
{
    if (state == NULL) {
        return;
    }

    if (state->eu.repeat_prefix == CPU8086_REPEAT_NONE) {
        finish_instruction(state);
        return;
    }

    state->general[CPU8086_REG_CX] =
        (uint16_t)(state->general[CPU8086_REG_CX] - 1u);
    if (!string_repeat_continues(state)) {
        finish_instruction(state);
        return;
    }

    /* The next cycle is an architectural REP interruption boundary. */
    state->eu.micro_step = CPU8086_STRING_STEP_REPEAT_BOUNDARY;
}

static void execute_string(Cpu8086State *state)
{
    Cpu8086AluResult alu_result;
    uint8_t width_bytes;
    uint16_t accumulator;

    if (state == NULL) {
        return;
    }

    if (state->eu.micro_step == CPU8086_STRING_STEP_REPEAT_BOUNDARY) {
        state->eu.micro_step = CPU8086_STRING_STEP_START;
        return;
    }

    if (state->eu.micro_step == CPU8086_STRING_STEP_START &&
        state->eu.repeat_prefix != CPU8086_REPEAT_NONE &&
        state->general[CPU8086_REG_CX] == 0u) {
        finish_instruction(state);
        return;
    }

    width_bytes = string_width_bytes(&state->eu);
    switch (state->eu.opcode) {
    case 0xA4u:
    case 0xA5u:
        if (state->eu.micro_step == CPU8086_STRING_STEP_START) {
            if (memory_read(state,
                            string_source_physical_address(state),
                            width_bytes,
                            &state->eu.temporary[0u])) {
                state->eu.micro_step = CPU8086_STRING_STEP_SECOND;
            }
            return;
        }
        if (state->eu.micro_step == CPU8086_STRING_STEP_SECOND &&
            memory_write(state,
                         string_destination_physical_address(state),
                         width_bytes,
                         state->eu.temporary[0u],
                         SIM_BUS_LOCK_NONE)) {
            advance_string_index(state, CPU8086_REG_SI, width_bytes);
            advance_string_index(state, CPU8086_REG_DI, width_bytes);
            finish_string_iteration(state);
        }
        return;

    case 0xA6u:
    case 0xA7u:
        if (state->eu.micro_step == CPU8086_STRING_STEP_START) {
            if (memory_read(state,
                            string_source_physical_address(state),
                            width_bytes,
                            &state->eu.temporary[0u])) {
                state->eu.micro_step = CPU8086_STRING_STEP_SECOND;
            }
            return;
        }
        if (state->eu.micro_step == CPU8086_STRING_STEP_SECOND) {
            if (memory_read(state,
                            string_destination_physical_address(state),
                            width_bytes,
                            &state->eu.temporary[1u])) {
                state->eu.micro_step = CPU8086_STRING_STEP_FINISH;
            }
            return;
        }
        if (state->eu.micro_step == CPU8086_STRING_STEP_FINISH) {
            alu_result = cpu8086_alu_execute(CPU8086_ALU_SUB,
                                              state->eu.temporary[0u],
                                              state->eu.temporary[1u],
                                              false,
                                              instruction_width(&state->eu));
            state->flags = cpu8086_apply_alu_flags(state->flags, &alu_result);
            advance_string_index(state, CPU8086_REG_SI, width_bytes);
            advance_string_index(state, CPU8086_REG_DI, width_bytes);
            finish_string_iteration(state);
        }
        return;

    case 0xAAu:
    case 0xABu:
        accumulator = state->eu.operand_width_8
            ? cpu8086_get_reg8(state, CPU8086_REG_AX)
            : state->general[CPU8086_REG_AX];
        if (state->eu.micro_step == CPU8086_STRING_STEP_START &&
            memory_write(state,
                         string_destination_physical_address(state),
                         width_bytes,
                         accumulator,
                         SIM_BUS_LOCK_NONE)) {
            advance_string_index(state, CPU8086_REG_DI, width_bytes);
            finish_string_iteration(state);
        }
        return;

    case 0xACu:
    case 0xADu:
        if (state->eu.micro_step == CPU8086_STRING_STEP_START) {
            if (memory_read(state,
                            string_source_physical_address(state),
                            width_bytes,
                            &state->eu.temporary[0u])) {
                state->eu.micro_step = CPU8086_STRING_STEP_SECOND;
            }
            return;
        }
        if (state->eu.micro_step == CPU8086_STRING_STEP_SECOND) {
            if (state->eu.operand_width_8) {
                cpu8086_set_reg8(state,
                                  CPU8086_REG_AX,
                                  (uint8_t)state->eu.temporary[0u]);
            } else {
                state->general[CPU8086_REG_AX] = state->eu.temporary[0u];
            }
            advance_string_index(state, CPU8086_REG_SI, width_bytes);
            finish_string_iteration(state);
        }
        return;

    case 0xAEu:
    case 0xAFu:
        if (state->eu.micro_step == CPU8086_STRING_STEP_START) {
            if (memory_read(state,
                            string_destination_physical_address(state),
                            width_bytes,
                            &state->eu.temporary[0u])) {
                state->eu.micro_step = CPU8086_STRING_STEP_SECOND;
            }
            return;
        }
        if (state->eu.micro_step == CPU8086_STRING_STEP_SECOND) {
            accumulator = state->eu.operand_width_8
                ? cpu8086_get_reg8(state, CPU8086_REG_AX)
                : state->general[CPU8086_REG_AX];
            alu_result = cpu8086_alu_execute(CPU8086_ALU_SUB,
                                              accumulator,
                                              state->eu.temporary[0u],
                                              false,
                                              instruction_width(&state->eu));
            state->flags = cpu8086_apply_alu_flags(state->flags, &alu_result);
            advance_string_index(state, CPU8086_REG_DI, width_bytes);
            finish_string_iteration(state);
        }
        return;

    default:
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }
}

static bool read_rm_with_lock(Cpu8086State *state,
                              uint16_t *value,
                              SimBusLockAction lock_action)
{
    Cpu8086ModRm modrm;
    uint32_t physical_address;

    if (state == NULL || value == NULL) {
        return false;
    }

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    if (modrm.mod == 3u) {
        *value = state->eu.operand_width_8
            ? cpu8086_get_reg8(state, modrm.rm)
            : cpu8086_get_reg16(state, modrm.rm);
        return true;
    }
    return rm_memory_address(state, &physical_address) &&
           memory_read_with_lock(state,
                                 physical_address,
                                 state->eu.operand_width_8 ? 1u : 2u,
                                 value,
                                 lock_action);
}

static bool read_rm(Cpu8086State *state, uint16_t *value)
{
    return read_rm_with_lock(state, value, SIM_BUS_LOCK_NONE);
}

static bool write_rm(Cpu8086State *state,
                     uint16_t value,
                     SimBusLockAction lock_action)
{
    Cpu8086ModRm modrm;
    uint32_t physical_address;

    if (state == NULL) {
        return false;
    }

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    if (modrm.mod == 3u) {
        if (state->eu.operand_width_8) {
            cpu8086_set_reg8(state, modrm.rm, (uint8_t)value);
        } else {
            cpu8086_set_reg16(state, modrm.rm, value);
        }
        return true;
    }
    return rm_memory_address(state, &physical_address) &&
           memory_write(state,
                        physical_address,
                        state->eu.operand_width_8 ? 1u : 2u,
                        value,
                        lock_action);
}

static uint16_t immediate_operand(const Cpu8086EuState *eu)
{
    uint16_t value = immediate_value(eu);

    if (eu != NULL && eu->immediate_sign_extend && !eu->operand_width_8) {
        value = (uint16_t)(int16_t)(int8_t)value;
    }
    return value;
}

static Cpu8086AluResult binary_alu(const Cpu8086State *state,
                                    uint8_t selector,
                                    uint16_t left,
                                    uint16_t right)
{
    bool carry = state != NULL && (state->flags & CPU8086_FLAG_CARRY) != 0u;
    uint8_t width = instruction_width(&state->eu);

    switch (selector & 0x07u) {
    case 0u:
        return cpu8086_alu_execute(CPU8086_ALU_ADD, left, right, false, width);
    case 1u:
        return cpu8086_alu_execute(CPU8086_ALU_OR, left, right, false, width);
    case 2u:
        return cpu8086_alu_execute(CPU8086_ALU_ADD, left, right, carry, width);
    case 3u:
        return cpu8086_alu_execute(CPU8086_ALU_SUB, left, right, carry, width);
    case 4u:
        return cpu8086_alu_execute(CPU8086_ALU_AND, left, right, false, width);
    case 5u:
        return cpu8086_alu_execute(CPU8086_ALU_SUB, left, right, false, width);
    case 6u:
        return cpu8086_alu_execute(CPU8086_ALU_XOR, left, right, false, width);
    case 7u:
    default:
        return cpu8086_alu_execute(CPU8086_ALU_SUB, left, right, false, width);
    }
}

static bool selector_writes_result(uint8_t selector)
{
    return (selector & 0x07u) != 7u;
}

static void execute_binary_to_rm(Cpu8086State *state,
                                 uint8_t selector,
                                 uint16_t source,
                                 bool write_result)
{
    Cpu8086AluResult result;
    SimBusLockAction read_lock_action;
    SimBusLockAction write_lock_action;

    read_lock_action = write_result && state->eu.lock_prefix &&
        !modrm_is_register(&state->eu)
        ? SIM_BUS_LOCK_ACQUIRE
        : SIM_BUS_LOCK_NONE;
    write_lock_action = read_lock_action == SIM_BUS_LOCK_ACQUIRE
        ? SIM_BUS_LOCK_RELEASE
        : SIM_BUS_LOCK_NONE;

    if (state->eu.micro_step == 0u) {
        if (!read_rm_with_lock(state, &state->eu.temporary[0u], read_lock_action)) {
            return;
        }
        state->eu.micro_step = 1u;
        return;
    }

    if (state->eu.micro_step == 1u) {
        result = binary_alu(state, selector, state->eu.temporary[0u], source);
        state->eu.temporary[1u] = result.value;
        state->flags = cpu8086_apply_alu_flags(state->flags, &result);
        if (!write_result) {
            finish_instruction(state);
            return;
        }
        state->eu.micro_step = 2u;
    }

    if (state->eu.micro_step == 2u &&
        write_rm(state, state->eu.temporary[1u], write_lock_action)) {
        finish_instruction(state);
    }
}

static void execute_binary_to_reg(Cpu8086State *state,
                                  uint8_t selector,
                                  uint8_t register_code,
                                  bool write_result)
{
    Cpu8086AluResult result;
    uint16_t left;
    uint16_t right;

    if (!read_rm(state, &right)) {
        return;
    }
    left = state->eu.operand_width_8
        ? cpu8086_get_reg8(state, register_code)
        : cpu8086_get_reg16(state, register_code);
    result = binary_alu(state, selector, left, right);
    state->flags = cpu8086_apply_alu_flags(state->flags, &result);
    if (write_result) {
        if (state->eu.operand_width_8) {
            cpu8086_set_reg8(state, register_code, (uint8_t)result.value);
        } else {
            cpu8086_set_reg16(state, register_code, result.value);
        }
    }
    finish_instruction(state);
}

static void execute_alu_rm_reg(Cpu8086State *state)
{
    Cpu8086ModRm modrm = cpu8086_decode_modrm(state->eu.modrm);
    uint8_t selector = (uint8_t)((state->eu.opcode >> 3u) & 0x07u);
    uint16_t source;

    if (state->eu.direction_to_reg) {
        execute_binary_to_reg(state, selector, modrm.reg, selector_writes_result(selector));
        return;
    }

    source = state->eu.operand_width_8
        ? cpu8086_get_reg8(state, modrm.reg)
        : cpu8086_get_reg16(state, modrm.reg);
    execute_binary_to_rm(state, selector, source, selector_writes_result(selector));
}

static void execute_alu_rm_imm(Cpu8086State *state)
{
    Cpu8086ModRm modrm = cpu8086_decode_modrm(state->eu.modrm);
    uint8_t selector = modrm.reg;

    execute_binary_to_rm(state,
                         selector,
                         immediate_operand(&state->eu),
                         selector_writes_result(selector));
}

static void execute_test_rm_reg(Cpu8086State *state)
{
    Cpu8086ModRm modrm = cpu8086_decode_modrm(state->eu.modrm);
    uint16_t source = state->eu.operand_width_8
        ? cpu8086_get_reg8(state, modrm.reg)
        : cpu8086_get_reg16(state, modrm.reg);

    execute_binary_to_rm(state, 4u, source, false);
}

static void execute_test_acc_imm(Cpu8086State *state)
{
    Cpu8086AluResult result;
    uint16_t accumulator = state->eu.operand_width_8
        ? cpu8086_get_reg8(state, CPU8086_REG_AX)
        : cpu8086_get_reg16(state, CPU8086_REG_AX);

    result = cpu8086_alu_execute(CPU8086_ALU_AND,
                                 accumulator,
                                 immediate_operand(&state->eu),
                                 false,
                                 instruction_width(&state->eu));
    state->flags = cpu8086_apply_alu_flags(state->flags, &result);
    finish_instruction(state);
}

static uint8_t shift_rotate_count(const Cpu8086State *state)
{
    if (state == NULL) {
        return 0u;
    }

    return state->eu.opcode == 0xD0u || state->eu.opcode == 0xD1u
        ? 1u
        : cpu8086_get_reg8(state, CPU8086_REG_CX);
}

static Cpu8086ShiftOperation shift_rotate_operation(uint8_t selector)
{
    switch (selector & 0x07u) {
    case 0u:
        return CPU8086_SHIFT_ROL;
    case 1u:
        return CPU8086_SHIFT_ROR;
    case 2u:
        return CPU8086_SHIFT_RCL;
    case 3u:
        return CPU8086_SHIFT_RCR;
    case 4u:
    case 6u: /* 8086 decodes the undocumented /6 form as SAL/SHL. */
        return CPU8086_SHIFT_SHL;
    case 5u:
        return CPU8086_SHIFT_SHR;
    case 7u:
    default:
        return CPU8086_SHIFT_SAR;
    }
}

static void execute_shift_rotate_rm(Cpu8086State *state)
{
    Cpu8086ShiftResult result;
    SimBusLockAction read_lock_action;
    SimBusLockAction write_lock_action;
    uint8_t selector;
    uint8_t count;

    if (state == NULL) {
        return;
    }

    selector = cpu8086_decode_modrm(state->eu.modrm).reg;
    read_lock_action = state->eu.lock_prefix && !modrm_is_register(&state->eu)
        ? SIM_BUS_LOCK_ACQUIRE
        : SIM_BUS_LOCK_NONE;
    write_lock_action = read_lock_action == SIM_BUS_LOCK_ACQUIRE
        ? SIM_BUS_LOCK_RELEASE
        : SIM_BUS_LOCK_NONE;

    if (state->eu.micro_step == 0u) {
        count = shift_rotate_count(state);
        if (count == 0u) {
            finish_instruction(state);
            return;
        }
        if (!read_rm_with_lock(state, &state->eu.temporary[0u], read_lock_action)) {
            return;
        }
        state->eu.temporary[2u] = count;
        state->eu.micro_step = 1u;
        return;
    }

    if (state->eu.micro_step == 1u) {
        result = cpu8086_shift_execute(shift_rotate_operation(selector),
                                       state->eu.temporary[0u],
                                       (state->flags & CPU8086_FLAG_CARRY) != 0u,
                                       (uint8_t)state->eu.temporary[2u],
                                       instruction_width(&state->eu));
        state->eu.temporary[1u] = result.value;
        state->flags = cpu8086_apply_shift_flags(state->flags, &result);
        state->eu.micro_step = 2u;
    }

    if (state->eu.micro_step == 2u &&
        write_rm(state, state->eu.temporary[1u], write_lock_action)) {
        finish_instruction(state);
    }
}

static uint32_t moffs_physical_address(const Cpu8086State *state)
{
    Cpu8086Segment segment;

    if (state == NULL) {
        return 0u;
    }

    segment = state->eu.segment_override ? state->eu.override_segment : CPU8086_SEG_DS;
    return cpu8086_physical_address(state->segment[segment], immediate_value(&state->eu));
}

static uint16_t io_port_number(const Cpu8086State *state)
{
    if (state == NULL) {
        return 0u;
    }

    return state->eu.immediate_bytes != 0u
        ? immediate_value(&state->eu)
        : state->general[CPU8086_REG_DX];
}

static uint32_t stack_physical_address(const Cpu8086State *state, uint16_t offset)
{
    return state == NULL ? 0u
        : cpu8086_physical_address(state->segment[CPU8086_SEG_SS], offset);
}

static bool push_word(Cpu8086State *state, uint16_t value)
{
    uint16_t stack_pointer;

    if (state == NULL) {
        return false;
    }

    if (state->eu.micro_step == 0u) {
        stack_pointer = (uint16_t)(state->general[CPU8086_REG_SP] - 2u);
        state->general[CPU8086_REG_SP] = stack_pointer;
        state->eu.temporary[0u] = value;
        state->eu.micro_step = 1u;
    }

    return memory_write(state,
                        stack_physical_address(state, state->general[CPU8086_REG_SP]),
                        2u,
                        state->eu.temporary[0u],
                        SIM_BUS_LOCK_NONE);
}

static bool push_word_at_step(Cpu8086State *state,
                              uint8_t begin_step,
                              uint8_t wait_step,
                              uint16_t value)
{
    uint16_t stack_pointer;

    if (state == NULL) {
        return false;
    }

    if (state->eu.micro_step == begin_step) {
        stack_pointer = (uint16_t)(state->general[CPU8086_REG_SP] - 2u);
        state->general[CPU8086_REG_SP] = stack_pointer;
        state->eu.micro_step = wait_step;
    }
    if (state->eu.micro_step != wait_step) {
        return false;
    }

    return memory_write(state,
                        stack_physical_address(state, state->general[CPU8086_REG_SP]),
                        2u,
                        value,
                        SIM_BUS_LOCK_NONE);
}

static bool pop_word(Cpu8086State *state, uint16_t *value)
{
    if (state == NULL || value == NULL) {
        return false;
    }

    if (state->eu.micro_step == 0u) {
        if (!memory_read(state,
                         stack_physical_address(state, state->general[CPU8086_REG_SP]),
                         2u,
                         &state->eu.temporary[0u])) {
            return false;
        }
        state->general[CPU8086_REG_SP] = (uint16_t)(state->general[CPU8086_REG_SP] + 2u);
        state->eu.micro_step = 1u;
    }

    *value = state->eu.temporary[0u];
    return true;
}

static int16_t relative_displacement(const Cpu8086EuState *eu)
{
    uint16_t value = immediate_value(eu);

    if (eu != NULL && eu->immediate_bytes == 1u) {
        return (int16_t)(int8_t)value;
    }
    return (int16_t)value;
}

static void complete_control_transfer(Cpu8086State *state,
                                      Cpu8086EuResult *result,
                                      uint16_t instruction_pointer)
{
    if (state == NULL || result == NULL) {
        return;
    }

    state->ip = instruction_pointer;
    finish_instruction(state);
    result->flush_prefetch = true;
    result->flush_fetch_ip = instruction_pointer;
}

static uint32_t interrupt_vector_physical_address(uint8_t vector)
{
    return (uint32_t)vector * 4u;
}

static void execute_interrupt_entry(Cpu8086State *state,
                                    Cpu8086EuResult *result,
                                    uint8_t vector,
                                    uint8_t first_step)
{
    uint32_t vector_address;
    uint8_t flags_wait_step;
    uint8_t code_segment_step;
    uint8_t code_segment_wait_step;
    uint8_t instruction_pointer_step;
    uint8_t instruction_pointer_wait_step;
    uint8_t vector_offset_step;
    uint8_t vector_segment_step;

    if (state == NULL || result == NULL) {
        return;
    }

    flags_wait_step = (uint8_t)(first_step + 1u);
    code_segment_step = (uint8_t)(first_step + 2u);
    code_segment_wait_step = (uint8_t)(first_step + 3u);
    instruction_pointer_step = (uint8_t)(first_step + 4u);
    instruction_pointer_wait_step = (uint8_t)(first_step + 5u);
    vector_offset_step = (uint8_t)(first_step + 6u);
    vector_segment_step = (uint8_t)(first_step + 7u);

    if (state->eu.micro_step == first_step ||
        state->eu.micro_step == flags_wait_step) {
        if (!push_word_at_step(state, first_step, flags_wait_step, state->flags)) {
            return;
        }
        state->eu.micro_step = code_segment_step;
        return;
    }

    if (state->eu.micro_step == code_segment_step ||
        state->eu.micro_step == code_segment_wait_step) {
        if (!push_word_at_step(state,
                               code_segment_step,
                               code_segment_wait_step,
                               state->segment[CPU8086_SEG_CS])) {
            return;
        }
        state->eu.micro_step = instruction_pointer_step;
        return;
    }

    if (state->eu.micro_step == instruction_pointer_step ||
        state->eu.micro_step == instruction_pointer_wait_step) {
        if (!push_word_at_step(state,
                               instruction_pointer_step,
                               instruction_pointer_wait_step,
                               state->ip)) {
            return;
        }
        state->flags &= (uint16_t)~(CPU8086_FLAG_TRAP | CPU8086_FLAG_INTERRUPT);
        state->eu.micro_step = vector_offset_step;
        return;
    }

    vector_address = interrupt_vector_physical_address(vector);
    if (state->eu.micro_step == vector_offset_step) {
        if (!memory_read(state, vector_address, 2u, &state->eu.temporary[0u])) {
            return;
        }
        state->eu.micro_step = vector_segment_step;
        return;
    }

    if (state->eu.micro_step == vector_segment_step &&
        memory_read(state,
                    (vector_address + 2u) & 0x000FFFFFu,
                    2u,
                    &state->eu.temporary[1u])) {
        state->segment[CPU8086_SEG_CS] = state->eu.temporary[1u];
        complete_control_transfer(state, result, state->eu.temporary[0u]);
    }
}

static void execute_software_interrupt(Cpu8086State *state,
                                       Cpu8086EuResult *result,
                                       uint8_t vector)
{
    execute_interrupt_entry(state, result, vector, 0u);
}

static void execute_external_nmi(Cpu8086State *state, Cpu8086EuResult *result)
{
    execute_interrupt_entry(state, result, 2u, 0u);
}

static void execute_single_step(Cpu8086State *state, Cpu8086EuResult *result)
{
    execute_interrupt_entry(state, result, 1u, 0u);
}

static void execute_external_intr(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t ignored;

    if (state == NULL || result == NULL) {
        return;
    }

    if (state->eu.micro_step == 0u) {
        if (!interrupt_acknowledge(state, &ignored)) {
            return;
        }
        state->eu.micro_step = 1u;
        return;
    }

    if (state->eu.micro_step == 1u) {
        if (!interrupt_acknowledge(state, &state->eu.temporary[3u])) {
            return;
        }
        state->eu.micro_step = 2u;
        return;
    }

    execute_interrupt_entry(state, result, (uint8_t)state->eu.temporary[3u], 2u);
}

static void execute_group3_not_neg(Cpu8086State *state, bool negate)
{
    Cpu8086AluResult alu_result;
    SimBusLockAction read_lock_action;
    SimBusLockAction write_lock_action;
    uint16_t mask;

    if (state == NULL) {
        return;
    }

    read_lock_action = state->eu.lock_prefix && !modrm_is_register(&state->eu)
        ? SIM_BUS_LOCK_ACQUIRE
        : SIM_BUS_LOCK_NONE;
    write_lock_action = read_lock_action == SIM_BUS_LOCK_ACQUIRE
        ? SIM_BUS_LOCK_RELEASE
        : SIM_BUS_LOCK_NONE;
    mask = state->eu.operand_width_8 ? 0x00FFu : 0xFFFFu;

    if (state->eu.micro_step == 0u) {
        if (!read_rm_with_lock(state, &state->eu.temporary[0u], read_lock_action)) {
            return;
        }
        state->eu.micro_step = 1u;
        return;
    }

    if (state->eu.micro_step == 1u) {
        if (negate) {
            alu_result = cpu8086_alu_execute(CPU8086_ALU_SUB,
                                              0u,
                                              state->eu.temporary[0u],
                                              false,
                                              instruction_width(&state->eu));
            state->eu.temporary[1u] = alu_result.value;
            state->flags = cpu8086_apply_alu_flags(state->flags, &alu_result);
        } else {
            state->eu.temporary[1u] = (uint16_t)(~state->eu.temporary[0u] & mask);
        }
        state->eu.micro_step = 2u;
    }

    if (state->eu.micro_step == 2u &&
        write_rm(state, state->eu.temporary[1u], write_lock_action)) {
        finish_instruction(state);
    }
}

static void execute_group3(Cpu8086State *state, Cpu8086EuResult *eu_result)
{
    Cpu8086ModRm modrm;
    Cpu8086MulDivOperation operation;
    Cpu8086MulDivResult muldiv_result;
    uint16_t operand;
    uint8_t selector;

    if (state == NULL || eu_result == NULL) {
        return;
    }

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    selector = modrm.reg;
    switch (selector) {
    case 0u:
        execute_binary_to_rm(state, 4u, immediate_operand(&state->eu), false);
        return;

    case 1u:
        /* The original 8086 has no invalid-opcode exception vector. */
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;

    case 2u:
        execute_group3_not_neg(state, false);
        return;

    case 3u:
        execute_group3_not_neg(state, true);
        return;

    case 4u:
        operation = CPU8086_MULDIV_MUL;
        break;

    case 5u:
        operation = CPU8086_MULDIV_IMUL;
        break;

    case 6u:
        operation = CPU8086_MULDIV_DIV;
        break;

    case 7u:
        operation = CPU8086_MULDIV_IDIV;
        break;

    default:
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    if (state->eu.micro_step == 0u) {
        if (!read_rm(state, &state->eu.temporary[0u])) {
            return;
        }
        state->eu.micro_step = 1u;
        return;
    }

    if (state->eu.micro_step != 1u) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_BUS_ERROR;
        return;
    }

    operand = state->eu.temporary[0u];
    muldiv_result = cpu8086_muldiv_execute(operation,
                                            state->general[CPU8086_REG_AX],
                                            state->general[CPU8086_REG_DX],
                                            operand,
                                            instruction_width(&state->eu));
    if (muldiv_result.divide_error) {
        /* #DE is a fault: IRET must restart at the first prefix/opcode byte. */
        state->ip = state->eu.instruction_start_ip;
        state->eu.instruction = CPU8086_INSTRUCTION_DIVIDE_ERROR;
        state->eu.micro_step = 0u;
        execute_interrupt_entry(state, eu_result, 0u, 0u);
        return;
    }

    state->general[CPU8086_REG_AX] = muldiv_result.ax;
    state->general[CPU8086_REG_DX] = muldiv_result.dx;
    state->flags = cpu8086_apply_muldiv_flags(state->flags, &muldiv_result);
    finish_instruction(state);
}

static bool pop_interrupt_word(Cpu8086State *state, uint16_t *value)
{
    uint16_t stack_pointer;

    if (state == NULL || value == NULL) {
        return false;
    }

    stack_pointer = state->general[CPU8086_REG_SP];
    if (!memory_read(state,
                     stack_physical_address(state, stack_pointer),
                     2u,
                     value)) {
        return false;
    }
    state->general[CPU8086_REG_SP] = (uint16_t)(stack_pointer + 2u);
    return true;
}

static void execute_iret(Cpu8086State *state, Cpu8086EuResult *result)
{
    if (state == NULL || result == NULL) {
        return;
    }

    if (state->eu.micro_step == 0u) {
        if (!pop_interrupt_word(state, &state->eu.temporary[0u])) {
            return;
        }
        state->eu.micro_step = 1u;
        return;
    }

    if (state->eu.micro_step == 1u) {
        if (!pop_interrupt_word(state, &state->eu.temporary[1u])) {
            return;
        }
        state->eu.micro_step = 2u;
        return;
    }

    if (state->eu.micro_step == 2u &&
        pop_interrupt_word(state, &state->eu.temporary[2u])) {
        state->flags = cpu8086_normalize_flags(state->eu.temporary[2u]);
        state->segment[CPU8086_SEG_CS] = state->eu.temporary[1u];
        complete_control_transfer(state, result, state->eu.temporary[0u]);
    }
}

static uint16_t far_immediate_segment(const Cpu8086EuState *eu)
{
    uint8_t start;

    if (eu == NULL || eu->immediate_bytes != 4u ||
        eu->operand_count < eu->operand_needed) {
        return 0u;
    }

    start = (uint8_t)(eu->operand_needed - eu->immediate_bytes);
    return (uint16_t)(eu->operands[start + 2u] |
                      ((uint16_t)eu->operands[start + 3u] << 8u));
}

static bool conditional_branch_is_taken(const Cpu8086State *state)
{
    uint16_t flags;
    bool carry;
    bool parity;
    bool zero;
    bool sign;
    bool overflow;

    if (state == NULL) {
        return false;
    }

    flags = state->flags;
    carry = (flags & CPU8086_FLAG_CARRY) != 0u;
    parity = (flags & CPU8086_FLAG_PARITY) != 0u;
    zero = (flags & CPU8086_FLAG_ZERO) != 0u;
    sign = (flags & CPU8086_FLAG_SIGN) != 0u;
    overflow = (flags & CPU8086_FLAG_OVERFLOW) != 0u;
    switch (state->eu.opcode & 0x0Fu) {
    case 0x0u:
        return overflow;
    case 0x1u:
        return !overflow;
    case 0x2u:
        return carry;
    case 0x3u:
        return !carry;
    case 0x4u:
        return zero;
    case 0x5u:
        return !zero;
    case 0x6u:
        return carry || zero;
    case 0x7u:
        return !carry && !zero;
    case 0x8u:
        return sign;
    case 0x9u:
        return !sign;
    case 0xAu:
        return parity;
    case 0xBu:
        return !parity;
    case 0xCu:
        return sign != overflow;
    case 0xDu:
        return sign == overflow;
    case 0xEu:
        return zero || sign != overflow;
    case 0xFu:
    default:
        return !zero && sign == overflow;
    }
}

static void execute_jcc_rel8(Cpu8086State *state, Cpu8086EuResult *result)
{
    if (conditional_branch_is_taken(state)) {
        complete_control_transfer(state,
                                  result,
                                  (uint16_t)(state->ip + relative_displacement(&state->eu)));
    } else {
        finish_instruction(state);
    }
}

static void execute_loop_rel8(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t counter;
    bool zero;
    bool taken;

    if (state == NULL) {
        return;
    }

    zero = (state->flags & CPU8086_FLAG_ZERO) != 0u;
    counter = (uint16_t)(state->general[CPU8086_REG_CX] - 1u);
    state->general[CPU8086_REG_CX] = counter;
    if (state->eu.opcode == 0xE0u) {
        taken = counter != 0u && !zero;
    } else if (state->eu.opcode == 0xE1u) {
        taken = counter != 0u && zero;
    } else {
        taken = counter != 0u;
    }

    if (taken) {
        complete_control_transfer(state,
                                  result,
                                  (uint16_t)(state->ip + relative_displacement(&state->eu)));
    } else {
        finish_instruction(state);
    }
}

static void execute_jcxz_rel8(Cpu8086State *state, Cpu8086EuResult *result)
{
    if (state != NULL && state->general[CPU8086_REG_CX] == 0u) {
        complete_control_transfer(state,
                                  result,
                                  (uint16_t)(state->ip + relative_displacement(&state->eu)));
    } else {
        finish_instruction(state);
    }
}

static void execute_jmp(Cpu8086State *state, Cpu8086EuResult *result)
{
    if (state == NULL) {
        return;
    }

    if (state->eu.instruction == CPU8086_INSTRUCTION_JMP_FAR_IMM) {
        state->segment[CPU8086_SEG_CS] = far_immediate_segment(&state->eu);
        complete_control_transfer(state, result, immediate_value(&state->eu));
        return;
    }

    complete_control_transfer(state,
                              result,
                              (uint16_t)(state->ip + relative_displacement(&state->eu)));
}

static void execute_call_rel16(Cpu8086State *state, Cpu8086EuResult *result)
{
    if (state == NULL) {
        return;
    }

    if (state->eu.micro_step == 0u) {
        state->eu.temporary[1u] =
            (uint16_t)(state->ip + relative_displacement(&state->eu));
    }
    if (push_word(state, state->ip)) {
        complete_control_transfer(state, result, state->eu.temporary[1u]);
    }
}

static void execute_call_far_imm(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t stack_pointer;

    if (state == NULL) {
        return;
    }

    if (state->eu.micro_step == 0u) {
        state->eu.temporary[1u] = immediate_value(&state->eu);
        state->eu.temporary[2u] = far_immediate_segment(&state->eu);
        state->eu.temporary[3u] = state->ip;
        stack_pointer = (uint16_t)(state->general[CPU8086_REG_SP] - 2u);
        state->general[CPU8086_REG_SP] = stack_pointer;
        state->eu.temporary[0u] = state->segment[CPU8086_SEG_CS];
        state->eu.micro_step = 1u;
    }

    if (state->eu.micro_step == 1u) {
        if (!memory_write(state,
                          stack_physical_address(state, state->general[CPU8086_REG_SP]),
                          2u,
                          state->eu.temporary[0u],
                          SIM_BUS_LOCK_NONE)) {
            return;
        }
        state->eu.micro_step = 2u;
    }

    if (state->eu.micro_step == 2u) {
        stack_pointer = (uint16_t)(state->general[CPU8086_REG_SP] - 2u);
        state->general[CPU8086_REG_SP] = stack_pointer;
        state->eu.temporary[0u] = state->eu.temporary[3u];
        state->eu.micro_step = 3u;
    }

    if (state->eu.micro_step == 3u &&
        memory_write(state,
                     stack_physical_address(state, state->general[CPU8086_REG_SP]),
                     2u,
                     state->eu.temporary[0u],
                     SIM_BUS_LOCK_NONE)) {
        state->segment[CPU8086_SEG_CS] = state->eu.temporary[2u];
        complete_control_transfer(state, result, state->eu.temporary[1u]);
    }
}

static void execute_ret_near(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t instruction_pointer;

    if (!pop_word(state, &instruction_pointer)) {
        return;
    }
    state->general[CPU8086_REG_SP] =
        (uint16_t)(state->general[CPU8086_REG_SP] + immediate_value(&state->eu));
    complete_control_transfer(state, result, instruction_pointer);
}

static void execute_ret_far(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t stack_pointer;

    if (state == NULL) {
        return;
    }

    if (state->eu.micro_step == 0u) {
        if (!pop_word(state, &state->eu.temporary[1u])) {
            return;
        }
        state->eu.micro_step = 2u;
        return;
    }

    if (state->eu.micro_step == 2u) {
        stack_pointer = state->general[CPU8086_REG_SP];
        if (!memory_read(state,
                         stack_physical_address(state, stack_pointer),
                         2u,
                         &state->eu.temporary[0u])) {
            return;
        }
        state->general[CPU8086_REG_SP] = (uint16_t)(stack_pointer + 2u);
        state->eu.micro_step = 3u;
    }

    if (state->eu.micro_step == 3u) {
        state->segment[CPU8086_SEG_CS] = state->eu.temporary[0u];
        state->general[CPU8086_REG_SP] = (uint16_t)(state->general[CPU8086_REG_SP] +
                                                     immediate_value(&state->eu));
        complete_control_transfer(state, result, state->eu.temporary[1u]);
    }
}

static bool read_group5_far_pointer(Cpu8086State *state,
                                    uint16_t *instruction_pointer,
                                    uint16_t *code_segment)
{
    Cpu8086ModRm modrm;
    uint32_t physical_address;

    if (state == NULL || instruction_pointer == NULL || code_segment == NULL) {
        return false;
    }

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    if (modrm.mod == 3u) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_ADDRESSING;
        return false;
    }
    if (!rm_memory_address(state, &physical_address)) {
        return false;
    }

    if (state->eu.micro_step == 0u) {
        if (!memory_read(state, physical_address, 2u, &state->eu.temporary[0u])) {
            return false;
        }
        state->eu.micro_step = 1u;
        return false;
    }

    if (state->eu.micro_step != 1u ||
        !memory_read(state,
                     (physical_address + 2u) & 0x000FFFFFu,
                     2u,
                     &state->eu.temporary[1u])) {
        return false;
    }

    *instruction_pointer = state->eu.temporary[0u];
    *code_segment = state->eu.temporary[1u];
    return true;
}

static void execute_group5_inc_dec(Cpu8086State *state, Cpu8086AluOperation operation)
{
    Cpu8086AluResult result;
    SimBusLockAction read_lock_action;
    SimBusLockAction write_lock_action;

    read_lock_action = state->eu.lock_prefix && !modrm_is_register(&state->eu)
        ? SIM_BUS_LOCK_ACQUIRE
        : SIM_BUS_LOCK_NONE;
    write_lock_action = read_lock_action == SIM_BUS_LOCK_ACQUIRE
        ? SIM_BUS_LOCK_RELEASE
        : SIM_BUS_LOCK_NONE;

    if (state->eu.micro_step == 0u) {
        if (!read_rm_with_lock(state, &state->eu.temporary[0u], read_lock_action)) {
            return;
        }
        state->eu.micro_step = 1u;
    }

    if (state->eu.micro_step == 1u) {
        result = cpu8086_alu_execute(operation,
                                     state->eu.temporary[0u],
                                     1u,
                                     false,
                                     16u);
        state->eu.temporary[1u] = result.value;
        state->flags = cpu8086_apply_alu_flags(state->flags, &result);
        state->eu.micro_step = 2u;
    }

    if (state->eu.micro_step == 2u &&
        write_rm(state, state->eu.temporary[1u], write_lock_action)) {
        finish_instruction(state);
    }
}

static void execute_group4(Cpu8086State *state)
{
    Cpu8086ModRm modrm;
    Cpu8086AluOperation operation;
    Cpu8086AluResult result;
    SimBusLockAction read_lock_action;
    SimBusLockAction write_lock_action;

    if (state == NULL) {
        return;
    }

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    if (modrm.reg == 0u) {
        operation = CPU8086_ALU_INC;
    } else if (modrm.reg == 1u) {
        operation = CPU8086_ALU_DEC;
    } else {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    read_lock_action = state->eu.lock_prefix && !modrm_is_register(&state->eu)
        ? SIM_BUS_LOCK_ACQUIRE
        : SIM_BUS_LOCK_NONE;
    write_lock_action = read_lock_action == SIM_BUS_LOCK_ACQUIRE
        ? SIM_BUS_LOCK_RELEASE
        : SIM_BUS_LOCK_NONE;

    if (state->eu.micro_step == 0u) {
        if (!read_rm_with_lock(state, &state->eu.temporary[0u], read_lock_action)) {
            return;
        }
        state->eu.micro_step = 1u;
    }

    if (state->eu.micro_step == 1u) {
        result = cpu8086_alu_execute(operation,
                                     state->eu.temporary[0u],
                                     1u,
                                     false,
                                     8u);
        state->eu.temporary[1u] = result.value;
        state->flags = cpu8086_apply_alu_flags(state->flags, &result);
        state->eu.micro_step = 2u;
    }

    if (state->eu.micro_step == 2u &&
        write_rm(state, state->eu.temporary[1u], write_lock_action)) {
        finish_instruction(state);
    }
}

static void execute_group5_near_call(Cpu8086State *state, Cpu8086EuResult *result)
{
    if (state->eu.micro_step == 0u) {
        if (!read_rm(state, &state->eu.temporary[0u])) {
            return;
        }
        state->eu.micro_step = 1u;
    }

    if ((state->eu.micro_step == 1u || state->eu.micro_step == 2u) &&
        push_word_at_step(state, 1u, 2u, state->ip)) {
        complete_control_transfer(state, result, state->eu.temporary[0u]);
    }
}

static void execute_group5_far_call(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t instruction_pointer;
    uint16_t code_segment;

    if (state == NULL) {
        return;
    }

    if (state->eu.micro_step == 0u || state->eu.micro_step == 1u) {
        if (!read_group5_far_pointer(state, &instruction_pointer, &code_segment)) {
            return;
        }
        state->eu.temporary[2u] = state->segment[CPU8086_SEG_CS];
        state->eu.temporary[3u] = state->ip;
        state->eu.micro_step = 2u;
    }

    if ((state->eu.micro_step == 2u || state->eu.micro_step == 3u) &&
        push_word_at_step(state, 2u, 3u, state->eu.temporary[2u])) {
        state->eu.micro_step = 4u;
    }

    if ((state->eu.micro_step == 4u || state->eu.micro_step == 5u) &&
        push_word_at_step(state, 4u, 5u, state->eu.temporary[3u])) {
        state->segment[CPU8086_SEG_CS] = state->eu.temporary[1u];
        complete_control_transfer(state, result, state->eu.temporary[0u]);
    }
}

static void execute_group5_near_jmp(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t instruction_pointer;

    if (!read_rm(state, &instruction_pointer)) {
        return;
    }
    complete_control_transfer(state, result, instruction_pointer);
}

static void execute_group5_far_jmp(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t instruction_pointer;
    uint16_t code_segment;

    if (!read_group5_far_pointer(state, &instruction_pointer, &code_segment)) {
        return;
    }
    state->segment[CPU8086_SEG_CS] = code_segment;
    complete_control_transfer(state, result, instruction_pointer);
}

static void execute_group5_push(Cpu8086State *state)
{
    if (state->eu.micro_step == 0u) {
        if (!read_rm(state, &state->eu.temporary[0u])) {
            return;
        }
        state->eu.micro_step = 1u;
    }

    if ((state->eu.micro_step == 1u || state->eu.micro_step == 2u) &&
        push_word_at_step(state, 1u, 2u, state->eu.temporary[0u])) {
        finish_instruction(state);
    }
}

static void execute_group5(Cpu8086State *state, Cpu8086EuResult *result)
{
    Cpu8086ModRm modrm;

    if (state == NULL) {
        return;
    }

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    switch (modrm.reg) {
    case 0u:
        execute_group5_inc_dec(state, CPU8086_ALU_INC);
        break;
    case 1u:
        execute_group5_inc_dec(state, CPU8086_ALU_DEC);
        break;
    case 2u:
        execute_group5_near_call(state, result);
        break;
    case 3u:
        execute_group5_far_call(state, result);
        break;
    case 4u:
        execute_group5_near_jmp(state, result);
        break;
    case 5u:
        execute_group5_far_jmp(state, result);
        break;
    case 6u:
        execute_group5_push(state);
        break;
    case 7u:
    default:
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        break;
    }
}

static void execute_mov_rm_imm(Cpu8086State *state)
{
    if (cpu8086_decode_modrm(state->eu.modrm).reg != 0u) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    if (write_rm(state, immediate_operand(&state->eu), SIM_BUS_LOCK_NONE)) {
        finish_instruction(state);
    }
}

static void execute_mov_moffs(Cpu8086State *state)
{
    uint16_t value;

    if (state->eu.direction_to_reg) {
        if (!memory_read(state,
                         moffs_physical_address(state),
                         state->eu.operand_width_8 ? 1u : 2u,
                         &value)) {
            return;
        }
        if (state->eu.operand_width_8) {
            cpu8086_set_reg8(state, CPU8086_REG_AX, (uint8_t)value);
        } else {
            cpu8086_set_reg16(state, CPU8086_REG_AX, value);
        }
    } else {
        value = state->eu.operand_width_8
            ? cpu8086_get_reg8(state, CPU8086_REG_AX)
            : cpu8086_get_reg16(state, CPU8086_REG_AX);
        if (!memory_write(state,
                          moffs_physical_address(state),
                          state->eu.operand_width_8 ? 1u : 2u,
                          value,
                          SIM_BUS_LOCK_NONE)) {
            return;
        }
    }
    finish_instruction(state);
}

static void execute_in(Cpu8086State *state)
{
    uint16_t value;

    if (state == NULL ||
        !io_read(state,
                 io_port_number(state),
                 state->eu.operand_width_8 ? 1u : 2u,
                 &value)) {
        return;
    }

    if (state->eu.operand_width_8) {
        cpu8086_set_reg8(state, CPU8086_REG_AX, (uint8_t)value);
    } else {
        state->general[CPU8086_REG_AX] = value;
    }
    finish_instruction(state);
}

static void execute_out(Cpu8086State *state)
{
    uint16_t value;

    if (state == NULL) {
        return;
    }

    value = state->eu.operand_width_8
        ? cpu8086_get_reg8(state, CPU8086_REG_AX)
        : state->general[CPU8086_REG_AX];
    if (io_write(state,
                 io_port_number(state),
                 state->eu.operand_width_8 ? 1u : 2u,
                 value)) {
        finish_instruction(state);
    }
}

static void execute_mov_rm_segment(Cpu8086State *state)
{
    Cpu8086ModRm modrm = cpu8086_decode_modrm(state->eu.modrm);

    if (modrm.reg >= CPU8086_SEG_COUNT) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    if (write_rm(state, state->segment[modrm.reg], SIM_BUS_LOCK_NONE)) {
        finish_instruction(state);
    }
}

static void execute_mov_segment_rm(Cpu8086State *state)
{
    Cpu8086ModRm modrm = cpu8086_decode_modrm(state->eu.modrm);
    uint16_t value;

    if (modrm.reg >= CPU8086_SEG_COUNT || modrm.reg == CPU8086_SEG_CS) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    if (!read_rm(state, &value)) {
        return;
    }
    state->segment[modrm.reg] = value;
    if (modrm.reg == CPU8086_SEG_SS) {
        arm_interrupt_shadow(state);
    }
    finish_instruction(state);
}

static void execute_lea(Cpu8086State *state)
{
    Cpu8086EffectiveAddress address;
    Cpu8086ModRm modrm = cpu8086_decode_modrm(state->eu.modrm);

    if (modrm.mod == 3u || !cpu8086_effective_address(state,
                                                        modrm,
                                                        modrm_displacement(&state->eu),
                                                        &address)) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_ADDRESSING;
        return;
    }

    cpu8086_set_reg16(state, modrm.reg, address.offset);
    finish_instruction(state);
}

static void execute_load_far_pointer(Cpu8086State *state)
{
    Cpu8086ModRm modrm = cpu8086_decode_modrm(state->eu.modrm);
    uint32_t physical_address;

    if (modrm.mod == 3u || !rm_memory_address(state, &physical_address)) {
        return;
    }

    if (state->eu.micro_step == 0u) {
        if (!memory_read(state, physical_address, 2u, &state->eu.temporary[0u])) {
            return;
        }
        state->eu.micro_step = 1u;
        return;
    }

    if (!memory_read(state,
                     (physical_address + 2u) & 0x000FFFFFu,
                     2u,
                     &state->eu.temporary[1u])) {
        return;
    }
    cpu8086_set_reg16(state, modrm.reg, state->eu.temporary[0u]);
    state->segment[state->eu.opcode == 0xC4u ? CPU8086_SEG_ES : CPU8086_SEG_DS] =
        state->eu.temporary[1u];
    finish_instruction(state);
}

static void execute_xlat(Cpu8086State *state)
{
    Cpu8086Segment segment;
    uint32_t physical_address;
    uint16_t value;

    segment = state->eu.segment_override ? state->eu.override_segment : CPU8086_SEG_DS;
    physical_address = cpu8086_physical_address(
        state->segment[segment],
        (uint16_t)(state->general[CPU8086_REG_BX] +
                   cpu8086_get_reg8(state, CPU8086_REG_AX)));
    if (!memory_read(state, physical_address, 1u, &value)) {
        return;
    }
    cpu8086_set_reg8(state, CPU8086_REG_AX, (uint8_t)value);
    finish_instruction(state);
}

static void execute_push_reg16(Cpu8086State *state)
{
    uint16_t value = state->general[state->eu.embedded_register];

    if (state->eu.embedded_register == CPU8086_REG_SP && state->eu.micro_step == 0u) {
        value = (uint16_t)(value - 2u);
    }
    if (push_word(state, value)) {
        finish_instruction(state);
    }
}

static void execute_pop_reg16(Cpu8086State *state)
{
    uint16_t value;

    if (!pop_word(state, &value)) {
        return;
    }
    cpu8086_set_reg16(state, state->eu.embedded_register, value);
    finish_instruction(state);
}

static void execute_push_segment(Cpu8086State *state)
{
    if (push_word(state, state->segment[state->eu.embedded_register])) {
        finish_instruction(state);
    }
}

static void execute_pop_segment(Cpu8086State *state, Cpu8086EuResult *result)
{
    uint16_t value;

    if (state == NULL || result == NULL || !pop_word(state, &value)) {
        return;
    }
    state->segment[state->eu.embedded_register] = value;
    if (state->eu.embedded_register == CPU8086_SEG_CS) {
        /* POP CS changes the instruction stream even though IP is unchanged. */
        result->flush_prefetch = true;
        result->flush_fetch_ip = state->ip;
    } else if (state->eu.embedded_register == CPU8086_SEG_SS) {
        arm_interrupt_shadow(state);
    }
    finish_instruction(state);
}

static void execute_pushf(Cpu8086State *state)
{
    if (push_word(state, state->flags)) {
        finish_instruction(state);
    }
}

static void execute_popf(Cpu8086State *state)
{
    uint16_t value;

    if (!pop_word(state, &value)) {
        return;
    }
    state->flags = cpu8086_normalize_flags(value);
    finish_instruction(state);
}

static void execute_pop_rm(Cpu8086State *state)
{
    uint16_t value;

    if (cpu8086_decode_modrm(state->eu.modrm).reg != 0u) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    if (state->eu.micro_step == 0u && !pop_word(state, &value)) {
        return;
    }
    if (state->eu.micro_step == 1u &&
        write_rm(state, state->eu.temporary[0u], SIM_BUS_LOCK_NONE)) {
        finish_instruction(state);
    }
}

static void execute_mov_rm_reg(Cpu8086State *state)
{
    Cpu8086ModRm modrm;
    uint16_t value;

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    if (state->eu.direction_to_reg) {
        if (!read_rm(state, &value)) {
            return;
        }
        if (state->eu.operand_width_8) {
            cpu8086_set_reg8(state, modrm.reg, (uint8_t)value);
        } else {
            cpu8086_set_reg16(state, modrm.reg, value);
        }
    } else {
        value = state->eu.operand_width_8
            ? cpu8086_get_reg8(state, modrm.reg)
            : cpu8086_get_reg16(state, modrm.reg);
        if (!write_rm(state, value, SIM_BUS_LOCK_NONE)) {
            return;
        }
    }
    finish_instruction(state);
}

static void execute_xchg_rm_reg(Cpu8086State *state)
{
    Cpu8086ModRm modrm;
    uint16_t left;
    uint16_t right;

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    if (modrm.mod == 3u) {
        if (state->eu.operand_width_8) {
            left = cpu8086_get_reg8(state, modrm.rm);
            right = cpu8086_get_reg8(state, modrm.reg);
            cpu8086_set_reg8(state, modrm.rm, (uint8_t)right);
            cpu8086_set_reg8(state, modrm.reg, (uint8_t)left);
        } else {
            left = cpu8086_get_reg16(state, modrm.rm);
            right = cpu8086_get_reg16(state, modrm.reg);
            cpu8086_set_reg16(state, modrm.rm, right);
            cpu8086_set_reg16(state, modrm.reg, left);
        }
        finish_instruction(state);
        return;
    }

    if (state->eu.micro_step == 0u) {
        if (!read_rm_with_lock(state,
                               &state->eu.temporary[0u],
                               SIM_BUS_LOCK_ACQUIRE)) {
            return;
        }
        state->eu.micro_step = 1u;
        return;
    }

    right = state->eu.operand_width_8
        ? cpu8086_get_reg8(state, modrm.reg)
        : cpu8086_get_reg16(state, modrm.reg);
    if (!write_rm(state, right, SIM_BUS_LOCK_RELEASE)) {
        return;
    }
    if (state->eu.operand_width_8) {
        cpu8086_set_reg8(state, modrm.reg, (uint8_t)state->eu.temporary[0u]);
    } else {
        cpu8086_set_reg16(state, modrm.reg, state->eu.temporary[0u]);
    }
    finish_instruction(state);
}

static void execute_alu_accumulator(Cpu8086State *state, Cpu8086AluOperation operation,
                                    bool write_result,
                                    bool use_carry)
{
    Cpu8086AluResult result;
    uint8_t width = instruction_width(&state->eu);
    uint16_t left = width == 8u ? cpu8086_get_reg8(state, CPU8086_REG_AX)
                                : cpu8086_get_reg16(state, CPU8086_REG_AX);

    result = cpu8086_alu_execute(operation,
                                 left,
                                 immediate_value(&state->eu),
                                 use_carry && (state->flags & CPU8086_FLAG_CARRY) != 0u,
                                 width);
    if (write_result) {
        if (width == 8u) {
            cpu8086_set_reg8(state, CPU8086_REG_AX, (uint8_t)result.value);
        } else {
            cpu8086_set_reg16(state, CPU8086_REG_AX, result.value);
        }
    }
    state->flags = cpu8086_apply_alu_flags(state->flags, &result);
}

static void execute_bcd(Cpu8086State *state, Cpu8086EuResult *eu_result)
{
    Cpu8086BcdOperation operation;
    Cpu8086BcdResult result;

    if (state == NULL || eu_result == NULL) {
        return;
    }

    switch (state->eu.instruction) {
    case CPU8086_INSTRUCTION_DAA:
        operation = CPU8086_BCD_DAA;
        break;
    case CPU8086_INSTRUCTION_DAS:
        operation = CPU8086_BCD_DAS;
        break;
    case CPU8086_INSTRUCTION_AAA:
        operation = CPU8086_BCD_AAA;
        break;
    case CPU8086_INSTRUCTION_AAS:
        operation = CPU8086_BCD_AAS;
        break;
    case CPU8086_INSTRUCTION_AAM:
        operation = CPU8086_BCD_AAM;
        break;
    case CPU8086_INSTRUCTION_AAD:
        operation = CPU8086_BCD_AAD;
        break;
    default:
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    result = cpu8086_bcd_execute(operation,
                                 state->general[CPU8086_REG_AX],
                                 state->flags,
                                 (uint8_t)immediate_value(&state->eu));
    if (result.divide_error) {
        /* AAM 00h is #DE: IRET restarts at the first prefix/opcode byte. */
        state->ip = state->eu.instruction_start_ip;
        state->eu.instruction = CPU8086_INSTRUCTION_DIVIDE_ERROR;
        state->eu.micro_step = 0u;
        execute_interrupt_entry(state, eu_result, 0u, 0u);
        return;
    }

    state->general[CPU8086_REG_AX] = result.ax;
    state->flags = cpu8086_apply_bcd_flags(state->flags, &result);
    finish_instruction(state);
}

static void execute_esc(Cpu8086State *state)
{
    Cpu8086ModRm modrm;
    uint32_t physical_address = 0u;

    if (state == NULL) {
        return;
    }

    modrm = cpu8086_decode_modrm(state->eu.modrm);
    if (modrm.mod != 3u && !esc_memory_address(state, &physical_address)) {
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_ADDRESSING;
        return;
    }

    /*
     * The 8086 only hands a decoded ESCAPE operation to its coprocessor.
     * It does not perform a normal CPU operand transaction for this address.
     */
    state->esc.sequence += 1u;
    state->esc.opcode = state->eu.opcode;
    state->esc.modrm = state->eu.modrm;
    state->esc.has_memory_operand = modrm.mod != 3u;
    state->esc.physical_address = physical_address;
    finish_instruction(state);
}

static void execute_instruction(Cpu8086State *state, Cpu8086EuResult *eu_result)
{
    Cpu8086AluResult result;
    uint16_t exchange;
    uint8_t ah;

    switch (state->eu.instruction) {
    case CPU8086_INSTRUCTION_NOP:
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_HLT:
        retire_interrupt_shadow(state);
        state->eu.phase = CPU8086_EU_HALTED;
        break;

    case CPU8086_INSTRUCTION_WAIT:
        /* Handled by cpu8086_eu_evaluate(), which samples TEST each cycle. */
        state->eu.phase = CPU8086_EU_WAIT_TEST;
        break;

    case CPU8086_INSTRUCTION_JCC_REL8:
        execute_jcc_rel8(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_LOOP_REL8:
        execute_loop_rel8(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_JCXZ_REL8:
        execute_jcxz_rel8(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_JMP_REL8:
    case CPU8086_INSTRUCTION_JMP_REL16:
    case CPU8086_INSTRUCTION_JMP_FAR_IMM:
        execute_jmp(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_CALL_REL16:
        execute_call_rel16(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_CALL_FAR_IMM:
        execute_call_far_imm(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_RET_NEAR:
        execute_ret_near(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_RET_FAR:
        execute_ret_far(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_GROUP5_RM16:
        execute_group5(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_MOV_REG8_IMM8:
        cpu8086_set_reg8(state, state->eu.embedded_register, state->eu.operands[0u]);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_MOV_REG16_IMM16:
        cpu8086_set_reg16(state, state->eu.embedded_register, immediate_value(&state->eu));
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_MOV_RM_REG:
        execute_mov_rm_reg(state);
        break;

    case CPU8086_INSTRUCTION_MOV_RM_IMM:
        execute_mov_rm_imm(state);
        break;

    case CPU8086_INSTRUCTION_MOV_MOFFS:
        execute_mov_moffs(state);
        break;

    case CPU8086_INSTRUCTION_MOV_RM_SEGMENT:
        execute_mov_rm_segment(state);
        break;

    case CPU8086_INSTRUCTION_MOV_SEGMENT_RM:
        execute_mov_segment_rm(state);
        break;

    case CPU8086_INSTRUCTION_LEA:
        execute_lea(state);
        break;

    case CPU8086_INSTRUCTION_LOAD_FAR_POINTER:
        execute_load_far_pointer(state);
        break;

    case CPU8086_INSTRUCTION_XLAT:
        execute_xlat(state);
        break;

    case CPU8086_INSTRUCTION_STRING:
        execute_string(state);
        break;

    case CPU8086_INSTRUCTION_PUSH_REG16:
        execute_push_reg16(state);
        break;

    case CPU8086_INSTRUCTION_POP_REG16:
        execute_pop_reg16(state);
        break;

    case CPU8086_INSTRUCTION_PUSH_SEGMENT:
        execute_push_segment(state);
        break;

    case CPU8086_INSTRUCTION_POP_SEGMENT:
        execute_pop_segment(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_PUSHF:
        execute_pushf(state);
        break;

    case CPU8086_INSTRUCTION_POPF:
        execute_popf(state);
        break;

    case CPU8086_INSTRUCTION_POP_RM:
        execute_pop_rm(state);
        break;

    case CPU8086_INSTRUCTION_ALU_RM_REG:
        execute_alu_rm_reg(state);
        break;

    case CPU8086_INSTRUCTION_ALU_RM_IMM:
        execute_alu_rm_imm(state);
        break;

    case CPU8086_INSTRUCTION_TEST_RM_REG:
        execute_test_rm_reg(state);
        break;

    case CPU8086_INSTRUCTION_TEST_ACC_IMM:
        execute_test_acc_imm(state);
        break;

    case CPU8086_INSTRUCTION_XCHG_AX_REG16:
        exchange = state->general[CPU8086_REG_AX];
        state->general[CPU8086_REG_AX] = state->general[state->eu.embedded_register];
        state->general[state->eu.embedded_register] = exchange;
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_XCHG_RM_REG:
        execute_xchg_rm_reg(state);
        break;

    case CPU8086_INSTRUCTION_SHIFT_ROTATE_RM:
        execute_shift_rotate_rm(state);
        break;

    case CPU8086_INSTRUCTION_GROUP3_RM:
        execute_group3(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_GROUP4_RM8:
        execute_group4(state);
        break;

    case CPU8086_INSTRUCTION_INC_REG16:
        result = cpu8086_alu_execute(CPU8086_ALU_INC,
                                     state->general[state->eu.embedded_register],
                                     0u,
                                     false,
                                     16u);
        state->general[state->eu.embedded_register] = result.value;
        state->flags = cpu8086_apply_alu_flags(state->flags, &result);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_DEC_REG16:
        result = cpu8086_alu_execute(CPU8086_ALU_DEC,
                                     state->general[state->eu.embedded_register],
                                     0u,
                                     false,
                                     16u);
        state->general[state->eu.embedded_register] = result.value;
        state->flags = cpu8086_apply_alu_flags(state->flags, &result);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_ADD_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_ADD, true, false);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_ADC_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_ADD, true, true);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_SUB_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_SUB, true, false);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_SBB_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_SUB, true, true);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_CMP_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_SUB, false, false);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_AND_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_AND, true, false);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_OR_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_OR, true, false);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_XOR_ACC_IMM:
        execute_alu_accumulator(state, CPU8086_ALU_XOR, true, false);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_CBW:
        state->general[CPU8086_REG_AX] = (uint16_t)(int16_t)(int8_t)
            cpu8086_get_reg8(state, CPU8086_REG_AX);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_CWD:
        state->general[CPU8086_REG_DX] = (state->general[CPU8086_REG_AX] & 0x8000u) != 0u
            ? 0xFFFFu
            : 0u;
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_DAA:
    case CPU8086_INSTRUCTION_DAS:
    case CPU8086_INSTRUCTION_AAA:
    case CPU8086_INSTRUCTION_AAS:
    case CPU8086_INSTRUCTION_AAM:
    case CPU8086_INSTRUCTION_AAD:
        execute_bcd(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_SALC:
        cpu8086_set_reg8(state,
                          CPU8086_REG_AX,
                          (state->flags & CPU8086_FLAG_CARRY) != 0u ? 0xFFu : 0u);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_LAHF:
        ah = (uint8_t)(state->flags & (CPU8086_FLAG_SIGN | CPU8086_FLAG_ZERO |
                                       CPU8086_FLAG_AUXILIARY_CARRY | CPU8086_FLAG_PARITY |
                                       CPU8086_FLAG_CARRY));
        cpu8086_set_reg8(state, 4u, (uint8_t)(ah | CPU8086_FLAG_RESERVED_1));
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_SAHF:
        ah = cpu8086_get_reg8(state, 4u);
        state->flags = (uint16_t)((state->flags & (uint16_t)~(CPU8086_FLAG_SIGN |
                                                               CPU8086_FLAG_ZERO |
                                                               CPU8086_FLAG_AUXILIARY_CARRY |
                                                               CPU8086_FLAG_PARITY |
                                                               CPU8086_FLAG_CARRY)) |
                                  (ah & (CPU8086_FLAG_SIGN | CPU8086_FLAG_ZERO |
                                         CPU8086_FLAG_AUXILIARY_CARRY | CPU8086_FLAG_PARITY |
                                         CPU8086_FLAG_CARRY)) |
                                  CPU8086_FLAG_RESERVED_1);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_CLC:
        state->flags &= (uint16_t)~CPU8086_FLAG_CARRY;
        finish_instruction(state);
        break;
    case CPU8086_INSTRUCTION_STC:
        state->flags |= CPU8086_FLAG_CARRY;
        finish_instruction(state);
        break;
    case CPU8086_INSTRUCTION_CMC:
        state->flags ^= CPU8086_FLAG_CARRY;
        finish_instruction(state);
        break;
    case CPU8086_INSTRUCTION_CLD:
        state->flags &= (uint16_t)~CPU8086_FLAG_DIRECTION;
        finish_instruction(state);
        break;
    case CPU8086_INSTRUCTION_STD:
        state->flags |= CPU8086_FLAG_DIRECTION;
        finish_instruction(state);
        break;
    case CPU8086_INSTRUCTION_CLI:
        state->flags &= (uint16_t)~CPU8086_FLAG_INTERRUPT;
        finish_instruction(state);
        break;
    case CPU8086_INSTRUCTION_STI:
        state->flags |= CPU8086_FLAG_INTERRUPT;
        arm_interrupt_shadow(state);
        finish_instruction(state);
        break;

    case CPU8086_INSTRUCTION_INT_IMM8:
        execute_software_interrupt(state,
                                   eu_result,
                                   (uint8_t)immediate_value(&state->eu));
        break;
    case CPU8086_INSTRUCTION_INT3:
        execute_software_interrupt(state, eu_result, 3u);
        break;
    case CPU8086_INSTRUCTION_INTO:
        if ((state->flags & CPU8086_FLAG_OVERFLOW) == 0u) {
            finish_instruction(state);
        } else {
            execute_software_interrupt(state, eu_result, 4u);
        }
        break;
    case CPU8086_INSTRUCTION_IRET:
        execute_iret(state, eu_result);
        break;
    case CPU8086_INSTRUCTION_IN:
        execute_in(state);
        break;
    case CPU8086_INSTRUCTION_OUT:
        execute_out(state);
        break;
    case CPU8086_INSTRUCTION_ESC:
        execute_esc(state);
        break;
    case CPU8086_INSTRUCTION_DIVIDE_ERROR:
        execute_interrupt_entry(state, eu_result, 0u, 0u);
        break;
    case CPU8086_INSTRUCTION_SINGLE_STEP:
        execute_single_step(state, eu_result);
        break;
    case CPU8086_INSTRUCTION_EXTERNAL_NMI:
        execute_external_nmi(state, eu_result);
        break;
    case CPU8086_INSTRUCTION_EXTERNAL_INTR:
        execute_external_intr(state, eu_result);
        break;

    case CPU8086_INSTRUCTION_UNSUPPORTED:
    default:
        state->eu.phase = CPU8086_EU_FAULTED;
        state->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        break;
    }
}

void cpu8086_eu_evaluate(const Cpu8086State *current,
                         Cpu8086State *next,
                         bool test_high,
                         Cpu8086EuResult *result)
{
    Cpu8086DecodedInstruction decoded;
    uint8_t byte;
    uint8_t operand_count;

    if (current == NULL || next == NULL || result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    if (current->eu.phase == CPU8086_EU_HALTED ||
        current->eu.phase == CPU8086_EU_FAULTED) {
        return;
    }

    if (current->eu.phase == CPU8086_EU_WAIT_TEST) {
        if (test_high) {
            finish_instruction(next);
            if ((current->flags & CPU8086_FLAG_TRAP) != 0u) {
                result->trap_boundary = true;
            }
        }
        return;
    }

    if (current->eu.phase == CPU8086_EU_EXECUTE) {
        execute_instruction(next, result);
        if (next->eu.phase == CPU8086_EU_NEED_OPCODE &&
            !instruction_is_interrupt_entry(current->eu.instruction) &&
            !instruction_updates_trap_flag(current->eu.instruction) &&
            (current->flags & CPU8086_FLAG_TRAP) != 0u) {
            result->trap_boundary = true;
        } else if (instruction_is_string(current->eu.instruction) &&
                   next->eu.phase == CPU8086_EU_EXECUTE &&
                   next->eu.micro_step == CPU8086_STRING_STEP_REPEAT_BOUNDARY &&
                   (current->flags & CPU8086_FLAG_TRAP) != 0u) {
            /* REP exposes a real instruction boundary after each committed iteration. */
            result->trap_boundary = true;
        }
        return;
    }

    if (!cpu8086_prefetch_peek(&current->prefetch, &byte)) {
        return;
    }

    result->consume_prefetch_byte = true;
    next->ip = (uint16_t)(current->ip + 1u);
    if (current->eu.phase == CPU8086_EU_NEED_OPCODE) {
        if (!current->eu.prefix_seen) {
            next->eu.instruction_start_ip = current->ip;
        }
        if (decode_prefix(byte, &next->eu)) {
            next->eu.prefix_seen = true;
            return;
        }
        decoded = cpu8086_decode_opcode(byte);
        next->eu.opcode = byte;
        next->eu.instruction = decoded.instruction;
        next->eu.embedded_register = decoded.embedded_register;
        next->eu.operand_count = 0u;
        next->eu.operand_needed = (uint8_t)(decoded.immediate_bytes +
                                             (decoded.needs_modrm ? 1u : 0u));
        next->eu.immediate_bytes = decoded.immediate_bytes;
        next->eu.needs_modrm = decoded.needs_modrm;
        next->eu.operand_width_8 = decoded.operand_width_8;
        next->eu.direction_to_reg = decoded.direction_to_reg;
        next->eu.immediate_sign_extend = decoded.immediate_sign_extend;
        if (decoded.instruction == CPU8086_INSTRUCTION_UNSUPPORTED) {
            next->eu.phase = CPU8086_EU_FAULTED;
            next->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        } else if (next->eu.operand_needed == 0u) {
            next->eu.phase = CPU8086_EU_EXECUTE;
        } else {
            next->eu.phase = CPU8086_EU_NEED_OPERANDS;
        }
        return;
    }

    if (current->eu.phase != CPU8086_EU_NEED_OPERANDS ||
        current->eu.operand_count >= current->eu.operand_needed ||
        current->eu.operand_count >= CPU8086_EU_OPERAND_CAPACITY) {
        next->eu.phase = CPU8086_EU_FAULTED;
        next->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
        return;
    }

    operand_count = current->eu.operand_count;
    next->eu.operands[operand_count] = byte;
    next->eu.operand_count = (uint8_t)(operand_count + 1u);
    if (current->eu.needs_modrm && operand_count == 0u) {
        uint8_t selector;

        next->eu.modrm = byte;
        next->eu.displacement_bytes = modrm_displacement_bytes(byte);
        next->eu.operand_needed = (uint8_t)(next->eu.operand_needed +
                                             next->eu.displacement_bytes);
        if (next->eu.instruction == CPU8086_INSTRUCTION_GROUP3_RM) {
            selector = (uint8_t)((byte >> 3u) & 0x07u);
            if (selector == 0u) {
                next->eu.immediate_bytes = next->eu.operand_width_8 ? 1u : 2u;
                next->eu.operand_needed = (uint8_t)(next->eu.operand_needed +
                                                     next->eu.immediate_bytes);
            } else if (selector == 1u) {
                /* 8086 has no #UD; model this undefined encoding as a stopped fault. */
                next->eu.phase = CPU8086_EU_FAULTED;
                next->eu.fault = CPU8086_FAULT_UNSUPPORTED_OPCODE;
                return;
            }
        }
    }
    if (next->eu.operand_count == next->eu.operand_needed) {
        next->eu.phase = CPU8086_EU_EXECUTE;
    }
}

static bool at_external_interrupt_boundary(const Cpu8086State *state)
{
    return state != NULL &&
           ((state->eu.phase == CPU8086_EU_NEED_OPCODE && !state->eu.prefix_seen) ||
            state->eu.phase == CPU8086_EU_WAIT_TEST ||
            state->eu.phase == CPU8086_EU_HALTED ||
            (state->eu.phase == CPU8086_EU_EXECUTE &&
             instruction_is_string(state->eu.instruction) &&
             state->eu.repeat_prefix != CPU8086_REPEAT_NONE &&
             state->eu.micro_step == CPU8086_STRING_STEP_REPEAT_BOUNDARY));
}

static void begin_external_interrupt(const Cpu8086State *current,
                                     Cpu8086State *next,
                                     Cpu8086Instruction instruction)
{
    if (current == NULL || next == NULL) {
        return;
    }

    next->eu.phase = CPU8086_EU_EXECUTE;
    next->eu.instruction = instruction;
    next->eu.opcode = 0u;
    next->eu.embedded_register = 0u;
    next->eu.operand_count = 0u;
    next->eu.operand_needed = 0u;
    next->eu.immediate_bytes = 0u;
    next->eu.displacement_bytes = 0u;
    next->eu.micro_step = 0u;
    next->eu.instruction_start_ip = current->ip;
    next->eu.needs_modrm = false;
    next->eu.operand_width_8 = false;
    next->eu.direction_to_reg = false;
    next->eu.immediate_sign_extend = false;
    next->eu.segment_override = false;
    next->eu.prefix_seen = false;
    next->eu.lock_prefix = false;
    next->eu.override_segment = CPU8086_SEG_DS;
    next->eu.repeat_prefix = CPU8086_REPEAT_NONE;
    memset(next->eu.temporary, 0, sizeof(next->eu.temporary));

    /* A REP interruption restarts at its first prefix, not after its opcode. */
    if (instruction_is_string(current->eu.instruction) &&
        current->eu.repeat_prefix != CPU8086_REPEAT_NONE &&
        current->eu.micro_step == CPU8086_STRING_STEP_REPEAT_BOUNDARY) {
        next->ip = current->eu.instruction_start_ip;
    } else if (current->eu.phase == CPU8086_EU_WAIT_TEST) {
        /* An interrupt during WAIT must return to WAIT, not skip the barrier. */
        next->ip = current->eu.instruction_start_ip;
    }
}

bool cpu8086_eu_accept_external_interrupt(const Cpu8086State *current,
                                          Cpu8086State *next,
                                          uint32_t irq_lines)
{
    bool nmi_line;
    bool nmi_pending;

    if (current == NULL || next == NULL) {
        return false;
    }

    nmi_line = (irq_lines & CPU8086_IRQ_LINE_NMI) != 0u;
    nmi_pending = current->eu.nmi_pending || (nmi_line && !current->eu.nmi_line);
    next->eu.nmi_line = nmi_line;
    next->eu.nmi_pending = nmi_pending;
    if (!at_external_interrupt_boundary(current)) {
        return false;
    }

    if (nmi_pending) {
        next->eu.nmi_pending = false;
        begin_external_interrupt(current, next, CPU8086_INSTRUCTION_EXTERNAL_NMI);
        return true;
    }

    if ((irq_lines & CPU8086_IRQ_LINE_INTR) != 0u &&
        (current->flags & CPU8086_FLAG_INTERRUPT) != 0u &&
        !current->eu.interrupt_shadow) {
        begin_external_interrupt(current, next, CPU8086_INSTRUCTION_EXTERNAL_INTR);
        return true;
    }

    return false;
}

bool cpu8086_eu_accept_trap(const Cpu8086State *current, Cpu8086State *next)
{
    if (current == NULL || next == NULL || !current->eu.trap_pending ||
        current->eu.interrupt_shadow ||
        !at_external_interrupt_boundary(current)) {
        return false;
    }

    next->eu.trap_pending = false;
    begin_external_interrupt(current, next, CPU8086_INSTRUCTION_SINGLE_STEP);
    return true;
}
