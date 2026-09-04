#ifndef SIM_CPU8086_STATE_H
#define SIM_CPU8086_STATE_H

#include "sim_bus.h"

#define CPU8086_PREFETCH_CAPACITY 6u
#define CPU8086_EU_OPERAND_CAPACITY 6u

typedef enum {
    CPU8086_REG_AX = 0,
    CPU8086_REG_CX,
    CPU8086_REG_DX,
    CPU8086_REG_BX,
    CPU8086_REG_SP,
    CPU8086_REG_BP,
    CPU8086_REG_SI,
    CPU8086_REG_DI,
    CPU8086_REG_COUNT
} Cpu8086Register;

typedef enum {
    CPU8086_SEG_ES = 0,
    CPU8086_SEG_CS,
    CPU8086_SEG_SS,
    CPU8086_SEG_DS,
    CPU8086_SEG_COUNT
} Cpu8086Segment;

enum {
    CPU8086_FLAG_CARRY = 0x0001u,
    CPU8086_FLAG_RESERVED_1 = 0x0002u,
    CPU8086_FLAG_PARITY = 0x0004u,
    CPU8086_FLAG_AUXILIARY_CARRY = 0x0010u,
    CPU8086_FLAG_ZERO = 0x0040u,
    CPU8086_FLAG_SIGN = 0x0080u,
    CPU8086_FLAG_TRAP = 0x0100u,
    CPU8086_FLAG_INTERRUPT = 0x0200u,
    CPU8086_FLAG_DIRECTION = 0x0400u,
    CPU8086_FLAG_OVERFLOW = 0x0800u
};

/* 8086/8088 status words read back with these reserved bits fixed. */
enum {
    CPU8086_FLAG_ALWAYS_SET = CPU8086_FLAG_RESERVED_1 | 0xF000u,
    CPU8086_FLAG_WRITABLE = CPU8086_FLAG_CARRY |
                            CPU8086_FLAG_PARITY |
                            CPU8086_FLAG_AUXILIARY_CARRY |
                            CPU8086_FLAG_ZERO |
                            CPU8086_FLAG_SIGN |
                            CPU8086_FLAG_TRAP |
                            CPU8086_FLAG_INTERRUPT |
                            CPU8086_FLAG_DIRECTION |
                            CPU8086_FLAG_OVERFLOW
};

typedef enum {
    CPU8086_EU_NEED_OPCODE = 0,
    CPU8086_EU_NEED_OPERANDS,
    CPU8086_EU_EXECUTE,
    CPU8086_EU_WAIT_TEST,
    CPU8086_EU_HALTED,
    CPU8086_EU_FAULTED
} Cpu8086EuPhase;

typedef enum {
    CPU8086_REPEAT_NONE = 0,
    CPU8086_REPEAT_WHILE_EQUAL,
    CPU8086_REPEAT_WHILE_NOT_EQUAL
} Cpu8086RepeatPrefix;

typedef enum {
    CPU8086_INSTRUCTION_UNSUPPORTED = 0,
    CPU8086_INSTRUCTION_NOP,
    CPU8086_INSTRUCTION_HLT,
    CPU8086_INSTRUCTION_WAIT,
    CPU8086_INSTRUCTION_JCC_REL8,
    CPU8086_INSTRUCTION_LOOP_REL8,
    CPU8086_INSTRUCTION_JCXZ_REL8,
    CPU8086_INSTRUCTION_JMP_REL8,
    CPU8086_INSTRUCTION_JMP_REL16,
    CPU8086_INSTRUCTION_JMP_FAR_IMM,
    CPU8086_INSTRUCTION_CALL_REL16,
    CPU8086_INSTRUCTION_CALL_FAR_IMM,
    CPU8086_INSTRUCTION_RET_NEAR,
    CPU8086_INSTRUCTION_RET_FAR,
    CPU8086_INSTRUCTION_GROUP5_RM16,
    CPU8086_INSTRUCTION_MOV_REG8_IMM8,
    CPU8086_INSTRUCTION_MOV_REG16_IMM16,
    CPU8086_INSTRUCTION_MOV_RM_REG,
    CPU8086_INSTRUCTION_MOV_RM_IMM,
    CPU8086_INSTRUCTION_MOV_MOFFS,
    CPU8086_INSTRUCTION_MOV_RM_SEGMENT,
    CPU8086_INSTRUCTION_MOV_SEGMENT_RM,
    CPU8086_INSTRUCTION_LEA,
    CPU8086_INSTRUCTION_LOAD_FAR_POINTER,
    CPU8086_INSTRUCTION_XLAT,
    CPU8086_INSTRUCTION_STRING,
    CPU8086_INSTRUCTION_PUSH_REG16,
    CPU8086_INSTRUCTION_POP_REG16,
    CPU8086_INSTRUCTION_PUSH_SEGMENT,
    CPU8086_INSTRUCTION_POP_SEGMENT,
    CPU8086_INSTRUCTION_PUSHF,
    CPU8086_INSTRUCTION_POPF,
    CPU8086_INSTRUCTION_POP_RM,
    CPU8086_INSTRUCTION_ALU_RM_REG,
    CPU8086_INSTRUCTION_ALU_RM_IMM,
    CPU8086_INSTRUCTION_TEST_RM_REG,
    CPU8086_INSTRUCTION_TEST_ACC_IMM,
    CPU8086_INSTRUCTION_XCHG_AX_REG16,
    CPU8086_INSTRUCTION_XCHG_RM_REG,
    CPU8086_INSTRUCTION_SHIFT_ROTATE_RM,
    CPU8086_INSTRUCTION_GROUP3_RM,
    CPU8086_INSTRUCTION_GROUP4_RM8,
    CPU8086_INSTRUCTION_INC_REG16,
    CPU8086_INSTRUCTION_DEC_REG16,
    CPU8086_INSTRUCTION_ADD_ACC_IMM,
    CPU8086_INSTRUCTION_ADC_ACC_IMM,
    CPU8086_INSTRUCTION_SUB_ACC_IMM,
    CPU8086_INSTRUCTION_SBB_ACC_IMM,
    CPU8086_INSTRUCTION_CMP_ACC_IMM,
    CPU8086_INSTRUCTION_AND_ACC_IMM,
    CPU8086_INSTRUCTION_OR_ACC_IMM,
    CPU8086_INSTRUCTION_XOR_ACC_IMM,
    CPU8086_INSTRUCTION_CBW,
    CPU8086_INSTRUCTION_CWD,
    CPU8086_INSTRUCTION_DAA,
    CPU8086_INSTRUCTION_DAS,
    CPU8086_INSTRUCTION_AAA,
    CPU8086_INSTRUCTION_AAS,
    CPU8086_INSTRUCTION_AAM,
    CPU8086_INSTRUCTION_AAD,
    CPU8086_INSTRUCTION_SALC,
    CPU8086_INSTRUCTION_LAHF,
    CPU8086_INSTRUCTION_SAHF,
    CPU8086_INSTRUCTION_CLC,
    CPU8086_INSTRUCTION_STC,
    CPU8086_INSTRUCTION_CMC,
    CPU8086_INSTRUCTION_CLD,
    CPU8086_INSTRUCTION_STD,
    CPU8086_INSTRUCTION_CLI,
    CPU8086_INSTRUCTION_STI,
    CPU8086_INSTRUCTION_INT_IMM8,
    CPU8086_INSTRUCTION_INT3,
    CPU8086_INSTRUCTION_INTO,
    CPU8086_INSTRUCTION_IRET,
    CPU8086_INSTRUCTION_IN,
    CPU8086_INSTRUCTION_OUT,
    CPU8086_INSTRUCTION_ESC,
    /* Internal pseudo-instructions; no opcode decodes directly to these values. */
    CPU8086_INSTRUCTION_DIVIDE_ERROR,
    CPU8086_INSTRUCTION_SINGLE_STEP,
    CPU8086_INSTRUCTION_EXTERNAL_NMI,
    CPU8086_INSTRUCTION_EXTERNAL_INTR
} Cpu8086Instruction;

typedef enum {
    CPU8086_FAULT_NONE = 0,
    CPU8086_FAULT_UNSUPPORTED_OPCODE,
    CPU8086_FAULT_UNSUPPORTED_ADDRESSING,
    CPU8086_FAULT_BUS_ERROR
} Cpu8086Fault;

typedef struct {
    uint8_t bytes[CPU8086_PREFETCH_CAPACITY];
    uint8_t head;
    uint8_t count;
} Cpu8086PrefetchState;

typedef enum {
    CPU8086_BIU_TRANSFER_IDLE = 0,
    CPU8086_BIU_TRANSFER_PENDING,
    CPU8086_BIU_TRANSFER_INFLIGHT,
    CPU8086_BIU_TRANSFER_COMPLETE
} Cpu8086BiuTransferPhase;

typedef enum {
    CPU8086_BIU_MEMORY = 0,
    CPU8086_BIU_IO,
    CPU8086_BIU_INTERRUPT_ACK
} Cpu8086BiuBusKind;

/* A request is architectural CPU state until the BIU has sampled its response. */
typedef struct {
    Cpu8086BiuTransferPhase phase;
    Cpu8086BiuBusKind kind;
    bool write;
    uint8_t width_bytes;
    uint8_t beat_index;
    uint32_t address;
    uint16_t data;
    uint64_t transaction_id;
    SimBusLockAction lock_action;
    bool error;
} Cpu8086BiuDataState;

typedef struct {
    uint16_t fetch_ip;
    bool fetch_inflight;
    uint32_t fetch_address;
    uint8_t fetch_width_bytes;
    uint8_t fetch_byte_enable;
    uint32_t prefetch_epoch;
    uint32_t fetch_epoch;
    uint64_t fetch_transaction_id;
    bool bus_lock_held;
    bool hold_pending;
    bool hlda;
    Cpu8086BiuDataState data;
} Cpu8086BiuState;

typedef struct {
    Cpu8086EuPhase phase;
    Cpu8086Instruction instruction;
    uint8_t opcode;
    uint8_t embedded_register;
    uint8_t modrm;
    uint8_t operand_count;
    uint8_t operand_needed;
    uint8_t immediate_bytes;
    uint8_t displacement_bytes;
    uint8_t micro_step;
    uint16_t instruction_start_ip;
    bool needs_modrm;
    bool operand_width_8;
    bool direction_to_reg;
    bool immediate_sign_extend;
    bool segment_override;
    bool prefix_seen;
    bool lock_prefix;
    bool interrupt_shadow;
    bool nmi_line;
    bool nmi_pending;
    bool trap_pending;
    Cpu8086Segment override_segment;
    Cpu8086RepeatPrefix repeat_prefix;
    uint16_t interrupt_shadow_cs;
    uint16_t interrupt_shadow_ip;
    uint16_t temporary[4];
    uint8_t operands[CPU8086_EU_OPERAND_CAPACITY];
    Cpu8086Fault fault;
} Cpu8086EuState;

/*
 * A registered ESCAPE output.  It is intentionally only an 8086-to-8087
 * notification: a future 8087 owns any operand BUS transactions itself.
 */
typedef struct {
    uint64_t sequence;
    uint8_t opcode;
    uint8_t modrm;
    bool has_memory_operand;
    uint32_t physical_address;
} Cpu8086EscRequest;

/* All clocked CPU state. Configuration and BUS wiring live outside this type. */
typedef struct {
    uint16_t general[CPU8086_REG_COUNT];
    uint16_t segment[CPU8086_SEG_COUNT];
    uint16_t ip;
    uint16_t flags;
    Cpu8086PrefetchState prefetch;
    Cpu8086BiuState biu;
    Cpu8086EuState eu;
    Cpu8086EscRequest esc;
} Cpu8086State;

void cpu8086_state_reset(Cpu8086State *state);
uint32_t cpu8086_physical_address(uint16_t segment, uint16_t offset);
uint16_t cpu8086_normalize_flags(uint16_t flags);
uint16_t cpu8086_get_reg16(const Cpu8086State *state, uint8_t register_code);
void cpu8086_set_reg16(Cpu8086State *state, uint8_t register_code, uint16_t value);
uint8_t cpu8086_get_reg8(const Cpu8086State *state, uint8_t register_code);
void cpu8086_set_reg8(Cpu8086State *state, uint8_t register_code, uint8_t value);

#endif
