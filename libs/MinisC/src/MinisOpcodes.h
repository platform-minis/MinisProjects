#pragma once
#include <stdint.h>

// Every instruction is 3 bytes: [opcode:1][operand:2 LE]
// PC always advances by 3, or by a jump.
// Jump targets are absolute offsets in the bytecode buffer.

enum Opcode : uint8_t {
    // ── Stack ─────────────────────────────────────────────────────────────
    OP_NOP          = 0x00,   // no-op
    OP_POP          = 0x01,   // discard top
    OP_DUP          = 0x02,   // duplicate top
    OP_DUP2         = 0x03,   // duplicate top two:  [a,b] → [a,b,a,b]
    OP_SWAP         = 0x04,   // swap top two

    // ── Push constants ────────────────────────────────────────────────────
    OP_PUSH_NULL    = 0x05,   // push null
    OP_PUSH_TRUE    = 0x06,   // push bool true
    OP_PUSH_FALSE   = 0x07,   // push bool false
    OP_PUSH_IMM     = 0x08,   // operand: signed int16  → push int32
    OP_PUSH_INT     = 0x09,   // operand: int pool idx  → push int32
    OP_PUSH_FLT     = 0x0A,   // operand: flt pool idx  → push float
    OP_PUSH_STR     = 0x0B,   // operand: str pool idx  → push string ref

    // ── Variables ─────────────────────────────────────────────────────────
    OP_LOAD_GLOBAL  = 0x0C,   // operand: global index
    OP_STORE_GLOBAL = 0x0D,
    OP_LOAD_LOCAL   = 0x0E,   // operand: local index (frame-relative)
    OP_STORE_LOCAL  = 0x0F,

    // ── Integer arithmetic ────────────────────────────────────────────────
    OP_IADD         = 0x10,
    OP_ISUB         = 0x11,
    OP_IMUL         = 0x12,
    OP_IDIV         = 0x13,   // abort on div/0
    OP_IMOD         = 0x14,   // abort on mod/0
    OP_INEG         = 0x15,

    // ── Float arithmetic ──────────────────────────────────────────────────
    OP_FADD         = 0x16,
    OP_FSUB         = 0x17,
    OP_FMUL         = 0x18,
    OP_FDIV         = 0x19,   // abort on div/0
    OP_FNEG         = 0x1A,

    // ── Bitwise ───────────────────────────────────────────────────────────
    OP_BAND         = 0x1B,
    OP_BOR          = 0x1C,
    OP_BXOR         = 0x1D,
    OP_BNOT         = 0x1E,
    OP_BSHL         = 0x1F,
    OP_BSHR         = 0x20,

    // ── Integer comparison (push bool) ────────────────────────────────────
    OP_IEQ          = 0x21,
    OP_INE          = 0x22,
    OP_ILT          = 0x23,
    OP_ILE          = 0x24,
    OP_IGT          = 0x25,
    OP_IGE          = 0x26,

    // ── Float comparison (push bool) ──────────────────────────────────────
    OP_FEQ          = 0x27,
    OP_FNE          = 0x28,
    OP_FLT          = 0x29,
    OP_FLE          = 0x2A,
    OP_FGT          = 0x2B,
    OP_FGE          = 0x2C,

    // ── String comparison (push bool) ─────────────────────────────────────
    OP_SEQ          = 0x2D,
    OP_SNE          = 0x2E,

    // ── Logical ───────────────────────────────────────────────────────────
    OP_LAND         = 0x2F,
    OP_LOR          = 0x30,
    OP_LNOT         = 0x31,

    // ── Type conversions ──────────────────────────────────────────────────
    OP_I2F          = 0x32,   // int → float
    OP_F2I          = 0x33,   // float → int (truncate)
    OP_I2B          = 0x34,   // int → bool (0 = false)
    OP_F2B          = 0x35,   // float → bool (0.0 = false)
    OP_I2S          = 0x36,   // int → string (decimal)
    OP_F2S          = 0x37,   // float → string
    OP_B2S          = 0x38,   // bool → "true"/"false"
    OP_S2I          = 0x39,   // string → int (atoi)
    OP_S2F          = 0x3A,   // string → float (atof)

    // ── Control flow ──────────────────────────────────────────────────────
    OP_JUMP         = 0x3B,   // operand: absolute target addr
    OP_JUMP_TRUE    = 0x3C,   // pop cond, jump if truthy
    OP_JUMP_FALSE   = 0x3D,   // pop cond, jump if falsy
    OP_JUMP_TRUE_K  = 0x3E,   // keep cond on stack, jump if truthy  (&&)
    OP_JUMP_FALSE_K = 0x3F,   // keep cond on stack, jump if falsy   (||)

    // ── Functions ─────────────────────────────────────────────────────────
    OP_CALL         = 0x40,   // operand: function index
    OP_CALL_NATIVE  = 0x41,   // operand: native index
    OP_RETURN       = 0x42,   // return void
    OP_RETURN_VAL   = 0x43,   // pop return value, return

    // ── Arrays ────────────────────────────────────────────────────────────
    OP_NEW_ARRAY    = 0x44,   // operand: size (0 = pop from stack)
    OP_ARR_GET      = 0x45,   // pop idx, pop arr → push arr[idx]
    OP_ARR_SET      = 0x46,   // pop val, pop idx, pop arr → arr[idx]=val
    OP_ARR_LEN      = 0x47,   // pop arr → push length

    // ── String operations ─────────────────────────────────────────────────
    OP_STR_CAT      = 0x48,   // pop b, pop a → push a+b
    OP_STR_LEN      = 0x49,   // pop s → push length (int)
    OP_STR_CHAR     = 0x4A,   // pop idx, pop s → push char code (int)

    // ── Optimized inc/dec (no stack use) ──────────────────────────────────
    OP_IINC_LOCAL   = 0x4B,   // operand: local index
    OP_IDEC_LOCAL   = 0x4C,
    OP_IINC_GLOBAL  = 0x4D,   // operand: global index
    OP_IDEC_GLOBAL  = 0x4E,
};

// Jump opcodes range (used by packer for target fixup)
static const uint8_t JUMP_OPCODE_FIRST = 0x3B;
static const uint8_t JUMP_OPCODE_LAST  = 0x3F;

inline bool isJumpOpcode(uint8_t op) {
    return op >= JUMP_OPCODE_FIRST && op <= JUMP_OPCODE_LAST;
}
