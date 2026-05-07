#include "MinisVM.h"
#include "MinisOpcodes.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Bytecode file format constants
static const uint8_t MBC_MAGIC0 = 0x4D;   // 'M'
static const uint8_t MBC_MAGIC1 = 0x43;   // 'C'
static const uint8_t MBC_VERSION = 0x01;
static const uint16_t FRAME_SENTINEL = 0xFFFF; // marks top-level main frame

// ─── Constructor / Reset ─────────────────────────────────────────────────────

MinisVM::MinisVM() {
    native_count = 0;
    reset();
}

void MinisVM::reset() {
    sp = 0;
    fp = 0;
    pc = 0;
    prog     = nullptr;
    prog_len = 0;
    has_error = false;
    err_buf[0] = '\0';

    for (uint8_t i = 0; i < MINIS_MAX_GLOBALS; i++) globals[i] = Value::makeNull();
    for (uint8_t i = 0; i < MINIS_STR_RT_MAX;  i++) str_rt_used[i] = false;
    for (uint8_t i = 0; i < MINIS_MAX_ARRAYS;  i++) { arr_used[i] = false; arr_len[i] = 0; }

    str_const_count = 0;
    func_count      = 0;
    int_count       = 0;
    flt_count       = 0;
}

// ─── Native registration ──────────────────────────────────────────────────────

bool MinisVM::registerNative(const char* name, NativeFn fn,
                              uint8_t argc, ValueType ret_type) {
    if (native_count >= MINIS_MAX_NATIVES) return false;
    natives[native_count++] = { name, fn, argc, ret_type };
    return true;
}

// ─── Bytecode load ────────────────────────────────────────────────────────────
// Format:
//   Header  [8]:  magic[2] version flags global_count func_count reserved[2]
//   IntPool:      count[2]  + int32[] × n
//   FltPool:      count[2]  + float[] × n
//   StrPool:      count[2]  + (len[1] + bytes) × n   (not null-terminated)
//   Functions:    for each → param_count[1] local_count[1] code_size[2] code[]

bool MinisVM::load(const uint8_t* bytecode, size_t len) {
    reset();

    if (len < 8)               { vm_abort("bytecode too short");  return false; }
    if (bytecode[0] != MBC_MAGIC0 ||
        bytecode[1] != MBC_MAGIC1) { vm_abort("bad magic");       return false; }
    if (bytecode[2] != MBC_VERSION){ vm_abort("version mismatch"); return false; }

    uint8_t global_count = bytecode[4];
    uint8_t n_funcs      = bytecode[5];

    if (global_count > MINIS_MAX_GLOBALS) { vm_abort("too many globals"); return false; }
    if (n_funcs      > MINIS_MAX_FUNCS)   { vm_abort("too many funcs");   return false; }

    uint16_t cur = 8;

#define NEED(n) if (cur + (n) > len) { vm_abort("truncated bytecode"); return false; }

    // ── Int pool ──
    NEED(2);
    uint16_t ni = (uint16_t)(bytecode[cur] | ((uint16_t)bytecode[cur+1] << 8)); cur += 2;
    if (ni > 32) { vm_abort("int pool overflow"); return false; }
    for (uint16_t i = 0; i < ni; i++) {
        NEED(4);
        int32_t v; memcpy(&v, bytecode + cur, 4); cur += 4;
        int_pool[i] = v;
    }
    int_count = (uint8_t)ni;

    // ── Float pool ──
    NEED(2);
    uint16_t nf = (uint16_t)(bytecode[cur] | ((uint16_t)bytecode[cur+1] << 8)); cur += 2;
    if (nf > 16) { vm_abort("flt pool overflow"); return false; }
    for (uint16_t i = 0; i < nf; i++) {
        NEED(4);
        float v; memcpy(&v, bytecode + cur, 4); cur += 4;
        flt_pool[i] = v;
    }
    flt_count = (uint8_t)nf;

    // ── String pool → copy into str_const ──
    NEED(2);
    uint16_t ns = (uint16_t)(bytecode[cur] | ((uint16_t)bytecode[cur+1] << 8)); cur += 2;
    if (ns > MINIS_STR_CONST_MAX) { vm_abort("str pool overflow"); return false; }
    for (uint16_t i = 0; i < ns; i++) {
        NEED(1);
        uint8_t slen = bytecode[cur++];
        NEED(slen);
        uint8_t copy = slen < (MINIS_STR_LEN - 1) ? slen : (MINIS_STR_LEN - 1);
        memcpy(str_const[i], bytecode + cur, copy);
        str_const[i][copy] = '\0';
        cur += slen;
    }
    str_const_count = (uint8_t)ns;

    // ── Function table ──
    for (uint8_t f = 0; f < n_funcs; f++) {
        NEED(4);
        funcs[f].param_count = bytecode[cur++];
        funcs[f].local_count = bytecode[cur++];
        uint16_t csz = (uint16_t)(bytecode[cur] | ((uint16_t)bytecode[cur+1] << 8)); cur += 2;
        NEED(csz);
        funcs[f].code_off = cur;
        cur += csz;
    }
    func_count = n_funcs;

#undef NEED

    if (func_count == 0) { vm_abort("no functions"); return false; }

    prog     = bytecode;
    prog_len = len;

    // Set up frame for main() (func 0, no params)
    pc = funcs[0].code_off;
    frames[0] = { FRAME_SENTINEL, 0, funcs[0].local_count };
    fp = 1;
    sp = funcs[0].local_count;
    for (uint8_t i = 0; i < sp; i++) stack[i] = Value::makeNull();

    return true;
}

// ─── Operand helpers ──────────────────────────────────────────────────────────

inline int16_t MinisVM::operand_i16() const {
    return (int16_t)((uint16_t)prog[pc+1] | ((uint16_t)prog[pc+2] << 8));
}
inline uint16_t MinisVM::operand_u16() const {
    return (uint16_t)prog[pc+1] | ((uint16_t)prog[pc+2] << 8);
}

// ─── Stack helpers ────────────────────────────────────────────────────────────

void MinisVM::push(Value v) {
    if (sp >= MINIS_STACK_SIZE) { vm_abort("stack overflow"); return; }
    stack[sp++] = v;
}

Value MinisVM::pop() {
    uint8_t base = (fp > 0) ? frames[fp-1].locals_base + frames[fp-1].local_count : 0;
    if (sp <= base) { vm_abort("stack underflow"); return Value::makeNull(); }
    return stack[--sp];
}

Value& MinisVM::peek() {
    return stack[sp - 1];
}

// ─── String helpers ───────────────────────────────────────────────────────────

const char* MinisVM::get_str(Value v) const {
    if (Value::strIsRuntime(v.ref))
        return str_rt[Value::strRtIndex(v.ref)];
    return str_const[v.ref];
}

uint8_t MinisVM::alloc_rt_str() {
    for (uint8_t i = 0; i < MINIS_STR_RT_MAX; i++) {
        if (!str_rt_used[i]) { str_rt_used[i] = true; str_rt[i][0] = '\0'; return i; }
    }
    vm_abort("runtime string pool exhausted");
    return 0;
}

void MinisVM::free_rt_str(uint8_t idx) {
    if (idx < MINIS_STR_RT_MAX) str_rt_used[idx] = false;
}

// ─── Array helpers ────────────────────────────────────────────────────────────

uint8_t MinisVM::alloc_arr(uint8_t size) {
    if (size > MINIS_ARRAY_ELEMS) { vm_abort("array too large"); return 0; }
    for (uint8_t i = 0; i < MINIS_MAX_ARRAYS; i++) {
        if (!arr_used[i]) {
            arr_used[i] = true;
            arr_len[i]  = size;
            for (uint8_t j = 0; j < size; j++) arr_data[i][j] = Value::makeNull();
            return i;
        }
    }
    vm_abort("array pool exhausted");
    return 0;
}

// ─── Error ────────────────────────────────────────────────────────────────────

void MinisVM::vm_abort(const char* msg) {
    strncpy(err_buf, msg, sizeof(err_buf) - 1);
    err_buf[sizeof(err_buf) - 1] = '\0';
    has_error = true;
}

// ─── Call / Return ────────────────────────────────────────────────────────────

MinisVM::Result MinisVM::call_func(uint16_t idx) {
    if (idx >= func_count)        { vm_abort("invalid func index"); return Result::ERROR; }
    if (fp >= MINIS_MAX_FRAMES)   { vm_abort("call stack overflow"); return Result::ERROR; }

    FuncEntry& f = funcs[idx];
    if (sp < f.param_count)       { vm_abort("too few args");        return Result::ERROR; }

    uint8_t base = sp - f.param_count;
    frames[fp++] = { (uint16_t)(pc + 3), base, f.local_count };

    // Initialize extra locals (beyond params)
    for (uint8_t i = f.param_count; i < f.local_count; i++) {
        if (sp >= MINIS_STACK_SIZE) { vm_abort("stack overflow in call"); return Result::ERROR; }
        stack[sp++] = Value::makeNull();
    }
    pc = f.code_off;
    return Result::RUNNING;
}

MinisVM::Result MinisVM::return_from(bool has_val) {
    if (fp == 0) { vm_abort("return with no frame"); return Result::ERROR; }

    Value ret;
    if (has_val) ret = pop();

    Frame& f = frames[--fp];
    sp = f.locals_base;   // discard all locals + args
    bool top_level = (f.return_pc == FRAME_SENTINEL);
    if (!top_level) pc = f.return_pc;

    if (has_val) push(ret);
    return top_level ? Result::DONE : Result::RUNNING;
}

// ─── Main run loop ────────────────────────────────────────────────────────────

MinisVM::Result MinisVM::run(uint32_t max_cycles) {
    if (!prog || has_error) return Result::ERROR;
    uint32_t n = 0;
    while (true) {
        if (has_error)     return Result::ERROR;
        if (pc >= prog_len){ vm_abort("pc out of bounds"); return Result::ERROR; }
        Result r = exec_one();
        if (r != Result::RUNNING) return r;
        if (max_cycles && ++n >= max_cycles) return Result::RUNNING;
    }
}

// ─── Single instruction ───────────────────────────────────────────────────────

MinisVM::Result MinisVM::exec_one() {
    Opcode op = (Opcode)prog[pc];

    switch (op) {

    // ── Stack ──────────────────────────────────────────────────────────────
    case OP_NOP:  pc += 3; break;
    case OP_POP:  pop();   pc += 3; break;

    case OP_DUP: {
        Value v = peek(); push(v); pc += 3; break;
    }
    case OP_DUP2: {
        if (sp < 2) { vm_abort("dup2 underflow"); return Result::ERROR; }
        Value a = stack[sp-2]; Value b = stack[sp-1];
        push(a); push(b); pc += 3; break;
    }
    case OP_SWAP: {
        if (sp < 2) { vm_abort("swap underflow"); return Result::ERROR; }
        Value t = stack[sp-1]; stack[sp-1] = stack[sp-2]; stack[sp-2] = t;
        pc += 3; break;
    }

    // ── Constants ──────────────────────────────────────────────────────────
    case OP_PUSH_NULL:  push(Value::makeNull());       pc += 3; break;
    case OP_PUSH_TRUE:  push(Value::makeBool(true));   pc += 3; break;
    case OP_PUSH_FALSE: push(Value::makeBool(false));  pc += 3; break;
    case OP_PUSH_IMM:   push(Value::makeInt((int32_t)operand_i16())); pc += 3; break;
    case OP_PUSH_INT: {
        uint16_t idx = operand_u16();
        if (idx >= int_count) { vm_abort("int pool OOB"); return Result::ERROR; }
        push(Value::makeInt(int_pool[idx])); pc += 3; break;
    }
    case OP_PUSH_FLT: {
        uint16_t idx = operand_u16();
        if (idx >= flt_count) { vm_abort("flt pool OOB"); return Result::ERROR; }
        push(Value::makeFloat(flt_pool[idx])); pc += 3; break;
    }
    case OP_PUSH_STR: {
        uint16_t idx = operand_u16();
        if (idx >= str_const_count) { vm_abort("str pool OOB"); return Result::ERROR; }
        push(Value::makeStr(Value::makeConstStrRef((uint8_t)idx))); pc += 3; break;
    }

    // ── Variables ──────────────────────────────────────────────────────────
    case OP_LOAD_GLOBAL: {
        uint16_t idx = operand_u16();
        if (idx >= MINIS_MAX_GLOBALS) { vm_abort("global OOB"); return Result::ERROR; }
        push(globals[idx]); pc += 3; break;
    }
    case OP_STORE_GLOBAL: {
        uint16_t idx = operand_u16();
        if (idx >= MINIS_MAX_GLOBALS) { vm_abort("global OOB"); return Result::ERROR; }
        globals[idx] = pop(); pc += 3; break;
    }
    case OP_LOAD_LOCAL: {
        uint16_t idx = operand_u16();
        Frame& fr = frames[fp-1];
        if (idx >= fr.local_count) { vm_abort("local OOB"); return Result::ERROR; }
        push(stack[fr.locals_base + idx]); pc += 3; break;
    }
    case OP_STORE_LOCAL: {
        uint16_t idx = operand_u16();
        Frame& fr = frames[fp-1];
        if (idx >= fr.local_count) { vm_abort("local OOB"); return Result::ERROR; }
        stack[fr.locals_base + idx] = pop(); pc += 3; break;
    }

    // ── Integer arithmetic ─────────────────────────────────────────────────
    case OP_IADD: { Value b=pop(),a=pop(); push(Value::makeInt(a.i+b.i)); pc+=3; break; }
    case OP_ISUB: { Value b=pop(),a=pop(); push(Value::makeInt(a.i-b.i)); pc+=3; break; }
    case OP_IMUL: { Value b=pop(),a=pop(); push(Value::makeInt(a.i*b.i)); pc+=3; break; }
    case OP_IDIV: {
        Value b=pop(),a=pop();
        if (b.i==0) { vm_abort("division by zero"); return Result::ERROR; }
        push(Value::makeInt(a.i/b.i)); pc+=3; break;
    }
    case OP_IMOD: {
        Value b=pop(),a=pop();
        if (b.i==0) { vm_abort("modulo by zero"); return Result::ERROR; }
        push(Value::makeInt(a.i%b.i)); pc+=3; break;
    }
    case OP_INEG: { Value a=pop(); push(Value::makeInt(-a.i)); pc+=3; break; }

    // ── Float arithmetic ───────────────────────────────────────────────────
    case OP_FADD: { Value b=pop(),a=pop(); push(Value::makeFloat(a.f+b.f)); pc+=3; break; }
    case OP_FSUB: { Value b=pop(),a=pop(); push(Value::makeFloat(a.f-b.f)); pc+=3; break; }
    case OP_FMUL: { Value b=pop(),a=pop(); push(Value::makeFloat(a.f*b.f)); pc+=3; break; }
    case OP_FDIV: {
        Value b=pop(),a=pop();
        if (b.f==0.0f) { vm_abort("float div by zero"); return Result::ERROR; }
        push(Value::makeFloat(a.f/b.f)); pc+=3; break;
    }
    case OP_FNEG: { Value a=pop(); push(Value::makeFloat(-a.f)); pc+=3; break; }

    // ── Bitwise ────────────────────────────────────────────────────────────
    case OP_BAND: { Value b=pop(),a=pop(); push(Value::makeInt(a.i&b.i)); pc+=3; break; }
    case OP_BOR:  { Value b=pop(),a=pop(); push(Value::makeInt(a.i|b.i)); pc+=3; break; }
    case OP_BXOR: { Value b=pop(),a=pop(); push(Value::makeInt(a.i^b.i)); pc+=3; break; }
    case OP_BNOT: { Value a=pop(); push(Value::makeInt(~a.i)); pc+=3; break; }
    case OP_BSHL: { Value b=pop(),a=pop(); push(Value::makeInt(a.i<<b.i)); pc+=3; break; }
    case OP_BSHR: { Value b=pop(),a=pop(); push(Value::makeInt(a.i>>b.i)); pc+=3; break; }

    // ── Integer comparison ─────────────────────────────────────────────────
    case OP_IEQ: { Value b=pop(),a=pop(); push(Value::makeBool(a.i==b.i)); pc+=3; break; }
    case OP_INE: { Value b=pop(),a=pop(); push(Value::makeBool(a.i!=b.i)); pc+=3; break; }
    case OP_ILT: { Value b=pop(),a=pop(); push(Value::makeBool(a.i<b.i));  pc+=3; break; }
    case OP_ILE: { Value b=pop(),a=pop(); push(Value::makeBool(a.i<=b.i)); pc+=3; break; }
    case OP_IGT: { Value b=pop(),a=pop(); push(Value::makeBool(a.i>b.i));  pc+=3; break; }
    case OP_IGE: { Value b=pop(),a=pop(); push(Value::makeBool(a.i>=b.i)); pc+=3; break; }

    // ── Float comparison ───────────────────────────────────────────────────
    case OP_FEQ: { Value b=pop(),a=pop(); push(Value::makeBool(a.f==b.f)); pc+=3; break; }
    case OP_FNE: { Value b=pop(),a=pop(); push(Value::makeBool(a.f!=b.f)); pc+=3; break; }
    case OP_FLT: { Value b=pop(),a=pop(); push(Value::makeBool(a.f<b.f));  pc+=3; break; }
    case OP_FLE: { Value b=pop(),a=pop(); push(Value::makeBool(a.f<=b.f)); pc+=3; break; }
    case OP_FGT: { Value b=pop(),a=pop(); push(Value::makeBool(a.f>b.f));  pc+=3; break; }
    case OP_FGE: { Value b=pop(),a=pop(); push(Value::makeBool(a.f>=b.f)); pc+=3; break; }

    // ── String comparison ──────────────────────────────────────────────────
    case OP_SEQ: {
        Value b=pop(),a=pop();
        push(Value::makeBool(strcmp(get_str(a), get_str(b)) == 0)); pc+=3; break;
    }
    case OP_SNE: {
        Value b=pop(),a=pop();
        push(Value::makeBool(strcmp(get_str(a), get_str(b)) != 0)); pc+=3; break;
    }

    // ── Logical ────────────────────────────────────────────────────────────
    case OP_LAND: { Value b=pop(),a=pop(); push(Value::makeBool(a.isTruthy()&&b.isTruthy())); pc+=3; break; }
    case OP_LOR:  { Value b=pop(),a=pop(); push(Value::makeBool(a.isTruthy()||b.isTruthy())); pc+=3; break; }
    case OP_LNOT: { Value a=pop(); push(Value::makeBool(!a.isTruthy())); pc+=3; break; }

    // ── Short-circuit jumps (keep top of stack) ────────────────────────────
    case OP_JUMP_TRUE_K: {
        uint16_t addr = operand_u16();
        if (peek().isTruthy()) pc = addr; else pc += 3;
        break;
    }
    case OP_JUMP_FALSE_K: {
        uint16_t addr = operand_u16();
        if (!peek().isTruthy()) pc = addr; else pc += 3;
        break;
    }

    // ── Type conversions ───────────────────────────────────────────────────
    case OP_I2F: { Value a=pop(); push(Value::makeFloat((float)a.i)); pc+=3; break; }
    case OP_F2I: { Value a=pop(); push(Value::makeInt((int32_t)a.f)); pc+=3; break; }
    case OP_I2B: { Value a=pop(); push(Value::makeBool(a.i!=0));      pc+=3; break; }
    case OP_F2B: { Value a=pop(); push(Value::makeBool(a.f!=0.0f));   pc+=3; break; }
    case OP_I2S: {
        Value a=pop();
        uint8_t s = alloc_rt_str(); if (has_error) return Result::ERROR;
        snprintf(str_rt[s], MINIS_STR_LEN, "%d", (int)a.i);
        push(Value::makeStr(Value::makeRtStrRef(s))); pc+=3; break;
    }
    case OP_F2S: {
        Value a=pop();
        uint8_t s = alloc_rt_str(); if (has_error) return Result::ERROR;
        snprintf(str_rt[s], MINIS_STR_LEN, "%.6g", a.f);
        push(Value::makeStr(Value::makeRtStrRef(s))); pc+=3; break;
    }
    case OP_B2S: {
        Value a=pop();
        uint8_t s = alloc_rt_str(); if (has_error) return Result::ERROR;
        strncpy(str_rt[s], a.b ? "true" : "false", MINIS_STR_LEN);
        push(Value::makeStr(Value::makeRtStrRef(s))); pc+=3; break;
    }
    case OP_S2I: {
        Value a=pop(); push(Value::makeInt(atoi(get_str(a)))); pc+=3; break;
    }
    case OP_S2F: {
        Value a=pop(); push(Value::makeFloat((float)atof(get_str(a)))); pc+=3; break;
    }

    // ── Control flow ───────────────────────────────────────────────────────
    case OP_JUMP:       pc = operand_u16(); break;
    case OP_JUMP_TRUE:  { uint16_t t=operand_u16(); pc = pop().isTruthy() ? t : pc+3; break; }
    case OP_JUMP_FALSE: { uint16_t t=operand_u16(); pc = !pop().isTruthy() ? t : pc+3; break; }

    // ── Functions ──────────────────────────────────────────────────────────
    case OP_CALL: {
        return call_func(operand_u16());
    }
    case OP_CALL_NATIVE: {
        uint16_t idx = operand_u16();
        if (idx >= native_count) { vm_abort("native OOB"); return Result::ERROR; }
        NativeDef& nd = natives[idx];
        if (sp < nd.argc) { vm_abort("too few args for native"); return Result::ERROR; }
        uint8_t base = sp - nd.argc;
        Value ret = nd.fn(stack + base, nd.argc);
        sp = base;
        if (nd.ret_type != T_NULL) push(ret);
        pc += 3; break;
    }
    case OP_RETURN:     return return_from(false);
    case OP_RETURN_VAL: return return_from(true);

    // ── Arrays ─────────────────────────────────────────────────────────────
    case OP_NEW_ARRAY: {
        uint16_t sz = operand_u16();
        if (sz == 0) { Value sv=pop(); sz=(uint16_t)sv.i; }
        uint8_t idx = alloc_arr((uint8_t)sz); if (has_error) return Result::ERROR;
        push(Value::makeArr(idx)); pc+=3; break;
    }
    case OP_ARR_GET: {
        Value idx_v=pop(), arr_v=pop();
        if (arr_v.type != T_ARRAY) { vm_abort("not an array"); return Result::ERROR; }
        uint8_t ai = (uint8_t)arr_v.ref; int32_t i = idx_v.i;
        if (i < 0 || i >= arr_len[ai]) { vm_abort("array index OOB"); return Result::ERROR; }
        push(arr_data[ai][i]); pc+=3; break;
    }
    case OP_ARR_SET: {
        Value val=pop(), idx_v=pop(), arr_v=pop();
        if (arr_v.type != T_ARRAY) { vm_abort("not an array"); return Result::ERROR; }
        uint8_t ai = (uint8_t)arr_v.ref; int32_t i = idx_v.i;
        if (i < 0 || i >= arr_len[ai]) { vm_abort("array index OOB"); return Result::ERROR; }
        arr_data[ai][i] = val; pc+=3; break;
    }
    case OP_ARR_LEN: {
        Value a=pop();
        if (a.type != T_ARRAY) { vm_abort("not an array"); return Result::ERROR; }
        push(Value::makeInt(arr_len[(uint8_t)a.ref])); pc+=3; break;
    }

    // ── String operations ──────────────────────────────────────────────────
    case OP_STR_CAT: {
        Value b=pop(), a=pop();
        const char* sa = get_str(a); const char* sb = get_str(b);
        uint8_t s = alloc_rt_str(); if (has_error) return Result::ERROR;
        strncpy(str_rt[s], sa, MINIS_STR_LEN - 1); str_rt[s][MINIS_STR_LEN-1] = '\0';
        size_t rem = MINIS_STR_LEN - 1 - strlen(str_rt[s]);
        strncat(str_rt[s], sb, rem);
        push(Value::makeStr(Value::makeRtStrRef(s))); pc+=3; break;
    }
    case OP_STR_LEN: {
        Value a=pop();
        push(Value::makeInt((int32_t)strlen(get_str(a)))); pc+=3; break;
    }
    case OP_STR_CHAR: {
        Value idx_v=pop(), str_v=pop();
        const char* s = get_str(str_v); int32_t i = idx_v.i;
        int32_t slen = (int32_t)strlen(s);
        if (i < 0 || i >= slen) { vm_abort("str_char OOB"); return Result::ERROR; }
        push(Value::makeInt((int32_t)(uint8_t)s[i])); pc+=3; break;
    }

    // ── Optimized inc/dec ──────────────────────────────────────────────────
    case OP_IINC_LOCAL: {
        Frame& fr=frames[fp-1]; stack[fr.locals_base + operand_u16()].i++; pc+=3; break;
    }
    case OP_IDEC_LOCAL: {
        Frame& fr=frames[fp-1]; stack[fr.locals_base + operand_u16()].i--; pc+=3; break;
    }
    case OP_IINC_GLOBAL: { globals[operand_u16()].i++; pc+=3; break; }
    case OP_IDEC_GLOBAL: { globals[operand_u16()].i--; pc+=3; break; }

    default:
        vm_abort("unknown opcode");
        return Result::ERROR;
    }

    return has_error ? Result::ERROR : Result::RUNNING;
}

// ─── Debug (Arduino only) ─────────────────────────────────────────────────────
#ifdef Arduino_h
static const char* valType(ValueType t) {
    switch (t) {
        case T_INT:    return "int";
        case T_FLOAT:  return "float";
        case T_BOOL:   return "bool";
        case T_STRING: return "string";
        case T_ARRAY:  return "array";
        default:       return "null";
    }
}

void MinisVM::dumpStack(Stream& out) const {
    out.printf("[Stack sp=%d]\n", sp);
    uint8_t base = (fp > 0) ? frames[fp-1].locals_base + frames[fp-1].local_count : 0;
    for (int i = (int)sp - 1; i >= (int)base; i--) {
        const Value& v = stack[i];
        out.printf("  [%d] %s: ", i, valType(v.type));
        switch (v.type) {
            case T_INT:    out.printf("%d\n", (int)v.i); break;
            case T_FLOAT:  out.printf("%.4g\n", v.f); break;
            case T_BOOL:   out.printf("%s\n", v.b ? "true" : "false"); break;
            case T_STRING: out.printf("\"%s\"\n", get_str(v)); break;
            default:       out.printf("null\n"); break;
        }
    }
}

void MinisVM::dumpGlobals(Stream& out) const {
    out.printf("[Globals]\n");
    for (int i = 0; i < MINIS_MAX_GLOBALS; i++) {
        if (globals[i].type == T_NULL) continue;
        const Value& v = globals[i];
        out.printf("  g[%d] %s: ", i, valType(v.type));
        switch (v.type) {
            case T_INT:    out.printf("%d\n", (int)v.i); break;
            case T_FLOAT:  out.printf("%.4g\n", v.f); break;
            case T_BOOL:   out.printf("%s\n", v.b ? "true" : "false"); break;
            case T_STRING: out.printf("\"%s\"\n", get_str(v)); break;
            default:       out.printf("null\n"); break;
        }
    }
}

void MinisVM::disassemble(Stream& out) const {
    if (!prog) { out.println("no program loaded"); return; }
    for (uint8_t f = 0; f < func_count; f++) {
        out.printf("--- func[%d] locals=%d params=%d ---\n",
                   f, funcs[f].local_count, funcs[f].param_count);
        uint16_t end = (f + 1 < func_count) ? funcs[f+1].code_off : (uint16_t)prog_len;
        for (uint16_t p = funcs[f].code_off; p < end && p + 2 < prog_len; p += 3) {
            Opcode op = (Opcode)prog[p];
            uint16_t operand = (uint16_t)prog[p+1] | ((uint16_t)prog[p+2] << 8);
            out.printf("  %04X  %02X %04X\n", p, (uint8_t)op, operand);
        }
    }
}
#endif
