#include "sim/cpu8086_decoder.h"

Cpu8086DecodedInstruction cpu8086_decode_opcode(uint8_t opcode)
{
    Cpu8086DecodedInstruction decoded;

    decoded.opcode = opcode;
    decoded.instruction = CPU8086_INSTRUCTION_UNSUPPORTED;
    decoded.embedded_register = 0u;
    decoded.immediate_bytes = 0u;
    decoded.needs_modrm = false;
    decoded.operand_width_8 = false;
    decoded.direction_to_reg = false;
    decoded.immediate_sign_extend = false;
    if (opcode <= 0x3Bu && (opcode & 0x07u) <= 3u) {
        decoded.instruction = CPU8086_INSTRUCTION_ALU_RM_REG;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = (opcode & 0x01u) == 0u;
        decoded.direction_to_reg = (opcode & 0x02u) != 0u;
    } else if (opcode == 0x80u || opcode == 0x81u || opcode == 0x82u ||
               opcode == 0x83u) {
        decoded.instruction = CPU8086_INSTRUCTION_ALU_RM_IMM;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = opcode == 0x80u || opcode == 0x82u;
        decoded.immediate_bytes = decoded.operand_width_8 || opcode == 0x83u ? 1u : 2u;
        decoded.immediate_sign_extend = opcode == 0x83u;
    } else if (opcode == 0x84u || opcode == 0x85u) {
        decoded.instruction = CPU8086_INSTRUCTION_TEST_RM_REG;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = opcode == 0x84u;
    } else if (opcode == 0xA8u || opcode == 0xA9u) {
        decoded.instruction = CPU8086_INSTRUCTION_TEST_ACC_IMM;
        decoded.operand_width_8 = opcode == 0xA8u;
        decoded.immediate_bytes = decoded.operand_width_8 ? 1u : 2u;
    } else if (opcode >= 0x50u && opcode <= 0x57u) {
        decoded.instruction = CPU8086_INSTRUCTION_PUSH_REG16;
        decoded.embedded_register = (uint8_t)(opcode & 0x07u);
    } else if (opcode >= 0x58u && opcode <= 0x5Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_POP_REG16;
        decoded.embedded_register = (uint8_t)(opcode & 0x07u);
    } else if (opcode == 0x06u || opcode == 0x0Eu || opcode == 0x16u ||
               opcode == 0x1Eu) {
        decoded.instruction = CPU8086_INSTRUCTION_PUSH_SEGMENT;
        decoded.embedded_register = opcode == 0x06u ? CPU8086_SEG_ES
                                  : opcode == 0x0Eu ? CPU8086_SEG_CS
                                  : opcode == 0x16u ? CPU8086_SEG_SS
                                                    : CPU8086_SEG_DS;
    } else if (opcode == 0x07u || opcode == 0x17u || opcode == 0x1Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_POP_SEGMENT;
        decoded.embedded_register = opcode == 0x07u ? CPU8086_SEG_ES
                                  : opcode == 0x17u ? CPU8086_SEG_SS
                                                    : CPU8086_SEG_DS;
    } else if (opcode == 0x0Fu) {
        /* 8086/8088 silicon extension: POP CS, removed on 80186 and later. */
        decoded.instruction = CPU8086_INSTRUCTION_POP_SEGMENT;
        decoded.embedded_register = CPU8086_SEG_CS;
    } else if (opcode == 0x9Cu) {
        decoded.instruction = CPU8086_INSTRUCTION_PUSHF;
    } else if (opcode == 0x9Du) {
        decoded.instruction = CPU8086_INSTRUCTION_POPF;
    } else if (opcode == 0x8Cu) {
        decoded.instruction = CPU8086_INSTRUCTION_MOV_RM_SEGMENT;
        decoded.needs_modrm = true;
    } else if (opcode == 0x8Eu) {
        decoded.instruction = CPU8086_INSTRUCTION_MOV_SEGMENT_RM;
        decoded.needs_modrm = true;
    } else if (opcode == 0x8Du) {
        decoded.instruction = CPU8086_INSTRUCTION_LEA;
        decoded.needs_modrm = true;
    } else if (opcode == 0x8Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_POP_RM;
        decoded.needs_modrm = true;
    } else if (opcode == 0xC4u || opcode == 0xC5u) {
        decoded.instruction = CPU8086_INSTRUCTION_LOAD_FAR_POINTER;
        decoded.needs_modrm = true;
    } else if (opcode == 0xC6u || opcode == 0xC7u) {
        decoded.instruction = CPU8086_INSTRUCTION_MOV_RM_IMM;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = opcode == 0xC6u;
        decoded.immediate_bytes = decoded.operand_width_8 ? 1u : 2u;
    } else if (opcode >= 0xA0u && opcode <= 0xA3u) {
        decoded.instruction = CPU8086_INSTRUCTION_MOV_MOFFS;
        decoded.immediate_bytes = 2u;
        decoded.operand_width_8 = (opcode & 0x01u) == 0u;
        decoded.direction_to_reg = (opcode & 0x02u) == 0u;
    } else if ((opcode >= 0xA4u && opcode <= 0xA7u) ||
               (opcode >= 0xAAu && opcode <= 0xAFu)) {
        decoded.instruction = CPU8086_INSTRUCTION_STRING;
        decoded.operand_width_8 = (opcode & 0x01u) == 0u;
    } else if (opcode == 0xD7u) {
        decoded.instruction = CPU8086_INSTRUCTION_XLAT;
    } else if (opcode == 0x90u) {
        decoded.instruction = CPU8086_INSTRUCTION_NOP;
    } else if (opcode == 0xF4u) {
        decoded.instruction = CPU8086_INSTRUCTION_HLT;
    } else if (opcode == 0x9Bu) {
        decoded.instruction = CPU8086_INSTRUCTION_WAIT;
    } else if (opcode == 0xCCu) {
        decoded.instruction = CPU8086_INSTRUCTION_INT3;
    } else if (opcode == 0xCDu) {
        decoded.instruction = CPU8086_INSTRUCTION_INT_IMM8;
        decoded.immediate_bytes = 1u;
    } else if (opcode == 0xCEu) {
        decoded.instruction = CPU8086_INSTRUCTION_INTO;
    } else if (opcode == 0xCFu) {
        decoded.instruction = CPU8086_INSTRUCTION_IRET;
    } else if (opcode == 0xE4u || opcode == 0xE5u ||
               opcode == 0xECu || opcode == 0xEDu) {
        decoded.instruction = CPU8086_INSTRUCTION_IN;
        decoded.operand_width_8 = (opcode & 0x01u) == 0u;
        decoded.immediate_bytes = opcode == 0xE4u || opcode == 0xE5u ? 1u : 0u;
    } else if (opcode == 0xE6u || opcode == 0xE7u ||
               opcode == 0xEEu || opcode == 0xEFu) {
        decoded.instruction = CPU8086_INSTRUCTION_OUT;
        decoded.operand_width_8 = (opcode & 0x01u) == 0u;
        decoded.immediate_bytes = opcode == 0xE6u || opcode == 0xE7u ? 1u : 0u;
    } else if (opcode >= 0xB0u && opcode <= 0xB7u) {
        decoded.instruction = CPU8086_INSTRUCTION_MOV_REG8_IMM8;
        decoded.embedded_register = (uint8_t)(opcode & 0x07u);
        decoded.immediate_bytes = 1u;
    } else if (opcode >= 0xB8u && opcode <= 0xBFu) {
        decoded.instruction = CPU8086_INSTRUCTION_MOV_REG16_IMM16;
        decoded.embedded_register = (uint8_t)(opcode & 0x07u);
        decoded.immediate_bytes = 2u;
    } else if (opcode >= 0x91u && opcode <= 0x97u) {
        decoded.instruction = CPU8086_INSTRUCTION_XCHG_AX_REG16;
        decoded.embedded_register = (uint8_t)(opcode & 0x07u);
    } else if (opcode >= 0x40u && opcode <= 0x47u) {
        decoded.instruction = CPU8086_INSTRUCTION_INC_REG16;
        decoded.embedded_register = (uint8_t)(opcode & 0x07u);
    } else if (opcode >= 0x48u && opcode <= 0x4Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_DEC_REG16;
        decoded.embedded_register = (uint8_t)(opcode & 0x07u);
    } else if (opcode == 0x04u || opcode == 0x05u) {
        decoded.instruction = CPU8086_INSTRUCTION_ADD_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x04u ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x04u;
    } else if (opcode == 0x14u || opcode == 0x15u) {
        decoded.instruction = CPU8086_INSTRUCTION_ADC_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x14u ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x14u;
    } else if (opcode == 0x2Cu || opcode == 0x2Du) {
        decoded.instruction = CPU8086_INSTRUCTION_SUB_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x2Cu ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x2Cu;
    } else if (opcode == 0x1Cu || opcode == 0x1Du) {
        decoded.instruction = CPU8086_INSTRUCTION_SBB_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x1Cu ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x1Cu;
    } else if (opcode == 0x3Cu || opcode == 0x3Du) {
        decoded.instruction = CPU8086_INSTRUCTION_CMP_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x3Cu ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x3Cu;
    } else if (opcode == 0x24u || opcode == 0x25u) {
        decoded.instruction = CPU8086_INSTRUCTION_AND_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x24u ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x24u;
    } else if (opcode == 0x0Cu || opcode == 0x0Du) {
        decoded.instruction = CPU8086_INSTRUCTION_OR_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x0Cu ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x0Cu;
    } else if (opcode == 0x34u || opcode == 0x35u) {
        decoded.instruction = CPU8086_INSTRUCTION_XOR_ACC_IMM;
        decoded.immediate_bytes = opcode == 0x34u ? 1u : 2u;
        decoded.operand_width_8 = opcode == 0x34u;
    } else if (opcode == 0x98u) {
        decoded.instruction = CPU8086_INSTRUCTION_CBW;
    } else if (opcode == 0x99u) {
        decoded.instruction = CPU8086_INSTRUCTION_CWD;
    } else if (opcode == 0x27u) {
        decoded.instruction = CPU8086_INSTRUCTION_DAA;
    } else if (opcode == 0x2Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_DAS;
    } else if (opcode == 0x37u) {
        decoded.instruction = CPU8086_INSTRUCTION_AAA;
    } else if (opcode == 0x3Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_AAS;
    } else if (opcode == 0xD4u) {
        decoded.instruction = CPU8086_INSTRUCTION_AAM;
        decoded.immediate_bytes = 1u;
    } else if (opcode == 0xD5u) {
        decoded.instruction = CPU8086_INSTRUCTION_AAD;
        decoded.immediate_bytes = 1u;
    } else if (opcode == 0xD6u) {
        /* Undocumented 8086 SALC/SETALC: AL = CF ? FFh : 00h. */
        decoded.instruction = CPU8086_INSTRUCTION_SALC;
    } else if (opcode == 0x9Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_LAHF;
    } else if (opcode == 0x9Eu) {
        decoded.instruction = CPU8086_INSTRUCTION_SAHF;
    } else if (opcode == 0xF8u) {
        decoded.instruction = CPU8086_INSTRUCTION_CLC;
    } else if (opcode == 0xF9u) {
        decoded.instruction = CPU8086_INSTRUCTION_STC;
    } else if (opcode == 0xF5u) {
        decoded.instruction = CPU8086_INSTRUCTION_CMC;
    } else if (opcode == 0xFCu) {
        decoded.instruction = CPU8086_INSTRUCTION_CLD;
    } else if (opcode == 0xFDu) {
        decoded.instruction = CPU8086_INSTRUCTION_STD;
    } else if (opcode == 0xFAu) {
        decoded.instruction = CPU8086_INSTRUCTION_CLI;
    } else if (opcode == 0xFBu) {
        decoded.instruction = CPU8086_INSTRUCTION_STI;
    } else if (opcode >= 0x70u && opcode <= 0x7Fu) {
        decoded.instruction = CPU8086_INSTRUCTION_JCC_REL8;
        decoded.immediate_bytes = 1u;
    } else if (opcode == 0x9Au) {
        decoded.instruction = CPU8086_INSTRUCTION_CALL_FAR_IMM;
        decoded.immediate_bytes = 4u;
    } else if (opcode == 0xC2u || opcode == 0xC3u) {
        decoded.instruction = CPU8086_INSTRUCTION_RET_NEAR;
        decoded.immediate_bytes = opcode == 0xC2u ? 2u : 0u;
    } else if (opcode == 0xCAu || opcode == 0xCBu) {
        decoded.instruction = CPU8086_INSTRUCTION_RET_FAR;
        decoded.immediate_bytes = opcode == 0xCAu ? 2u : 0u;
    } else if (opcode >= 0xE0u && opcode <= 0xE2u) {
        decoded.instruction = CPU8086_INSTRUCTION_LOOP_REL8;
        decoded.immediate_bytes = 1u;
    } else if (opcode == 0xE3u) {
        decoded.instruction = CPU8086_INSTRUCTION_JCXZ_REL8;
        decoded.immediate_bytes = 1u;
    } else if (opcode == 0xE8u) {
        decoded.instruction = CPU8086_INSTRUCTION_CALL_REL16;
        decoded.immediate_bytes = 2u;
    } else if (opcode == 0xE9u) {
        decoded.instruction = CPU8086_INSTRUCTION_JMP_REL16;
        decoded.immediate_bytes = 2u;
    } else if (opcode == 0xEAu) {
        decoded.instruction = CPU8086_INSTRUCTION_JMP_FAR_IMM;
        decoded.immediate_bytes = 4u;
    } else if (opcode == 0xEBu) {
        decoded.instruction = CPU8086_INSTRUCTION_JMP_REL8;
        decoded.immediate_bytes = 1u;
    } else if (opcode == 0xFFu) {
        decoded.instruction = CPU8086_INSTRUCTION_GROUP5_RM16;
        decoded.needs_modrm = true;
    } else if (opcode >= 0x88u && opcode <= 0x8Bu) {
        decoded.instruction = CPU8086_INSTRUCTION_MOV_RM_REG;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = (opcode & 0x01u) == 0u;
        decoded.direction_to_reg = (opcode & 0x02u) != 0u;
    } else if (opcode == 0x86u || opcode == 0x87u) {
        decoded.instruction = CPU8086_INSTRUCTION_XCHG_RM_REG;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = opcode == 0x86u;
    } else if (opcode >= 0xD0u && opcode <= 0xD3u) {
        decoded.instruction = CPU8086_INSTRUCTION_SHIFT_ROTATE_RM;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = (opcode & 0x01u) == 0u;
    } else if (opcode >= 0xD8u && opcode <= 0xDFu) {
        decoded.instruction = CPU8086_INSTRUCTION_ESC;
        decoded.needs_modrm = true;
    } else if (opcode == 0xFEu) {
        decoded.instruction = CPU8086_INSTRUCTION_GROUP4_RM8;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = true;
    } else if (opcode == 0xF6u || opcode == 0xF7u) {
        /* The immediate exists only for the /0 TEST subfunction. */
        decoded.instruction = CPU8086_INSTRUCTION_GROUP3_RM;
        decoded.needs_modrm = true;
        decoded.operand_width_8 = opcode == 0xF6u;
    }
    return decoded;
}
