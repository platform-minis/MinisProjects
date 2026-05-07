#pragma once
#include <stdint.h>
#include <stddef.h>
#include "MinisValue.h"
#include "MinisNatives.h"

// ── Configurable limits (override before including MinisC.h) ──────────────
#ifndef MINIS_STACK_SIZE
#define MINIS_STACK_SIZE       64    // operand stack depth
#endif
#ifndef MINIS_MAX_FRAMES
#define MINIS_MAX_FRAMES       16    // call depth
#endif
#ifndef MINIS_MAX_GLOBALS
#define MINIS_MAX_GLOBALS      32
#endif
#ifndef MINIS_MAX_NATIVES
#define MINIS_MAX_NATIVES      64
#endif
#ifndef MINIS_MAX_FUNCS
#define MINIS_MAX_FUNCS        16
#endif
#ifndef MINIS_STR_CONST_MAX
#define MINIS_STR_CONST_MAX    32    // string constants from bytecode pool
#endif
#ifndef MINIS_STR_RT_MAX
#define MINIS_STR_RT_MAX       8     // runtime strings (concat, int→str, etc.)
#endif
#ifndef MINIS_STR_LEN
#define MINIS_STR_LEN          48    // max chars per string (incl. null)
#endif
#ifndef MINIS_MAX_ARRAYS
#define MINIS_MAX_ARRAYS       8
#endif
#ifndef MINIS_ARRAY_ELEMS
#define MINIS_ARRAY_ELEMS      16    // max elements per array
#endif

class MinisVM {
public:
    enum class Result : uint8_t { RUNNING = 0, DONE = 1, ERROR = 2 };

    MinisVM();

    // Register a native C++ function. Must be called before load().
    bool registerNative(const char* name, NativeFn fn,
                        uint8_t argc, ValueType ret_type = T_NULL);

    // Load bytecode. Buffer must remain valid for the lifetime of execution.
    // Does NOT take ownership.
    bool load(const uint8_t* bytecode, size_t len);

    // Execute up to max_cycles instructions.
    // max_cycles == 0 → run until DONE or ERROR.
    // Returns RUNNING if budget exhausted but program not finished.
    Result run(uint32_t max_cycles = 0);

    // Reset VM state. Keeps registered natives. Unloads any program.
    void reset();

    bool        isLoaded()   const { return prog != nullptr; }
    const char* lastError()  const { return err_buf; }

#ifdef Arduino_h
    void dumpStack(Stream& out)   const;
    void dumpGlobals(Stream& out) const;
    void disassemble(Stream& out) const;
#endif

private:
    // ── Operand stack ────────────────────────────────────────────────────
    Value   stack[MINIS_STACK_SIZE];
    uint8_t sp;

    // ── Call frames ──────────────────────────────────────────────────────
    struct Frame {
        uint16_t return_pc;     // 0xFFFF = top-level sentinel
        uint8_t  locals_base;   // stack index of local[0]
        uint8_t  local_count;
    };
    Frame   frames[MINIS_MAX_FRAMES];
    uint8_t fp;

    // ── Global variables ─────────────────────────────────────────────────
    Value globals[MINIS_MAX_GLOBALS];

    // ── String pools ─────────────────────────────────────────────────────
    char    str_const[MINIS_STR_CONST_MAX][MINIS_STR_LEN]; // loaded from bytecode
    uint8_t str_const_count;
    char    str_rt[MINIS_STR_RT_MAX][MINIS_STR_LEN];       // runtime (concat etc.)
    bool    str_rt_used[MINIS_STR_RT_MAX];

    // ── Array pool ───────────────────────────────────────────────────────
    Value   arr_data[MINIS_MAX_ARRAYS][MINIS_ARRAY_ELEMS];
    uint8_t arr_len[MINIS_MAX_ARRAYS];
    bool    arr_used[MINIS_MAX_ARRAYS];

    // ── Native registry ──────────────────────────────────────────────────
    NativeDef natives[MINIS_MAX_NATIVES];
    uint8_t   native_count;

    // ── Bytecode ─────────────────────────────────────────────────────────
    const uint8_t* prog;
    size_t         prog_len;
    uint16_t       pc;

    // ── Constant pools (parsed from bytecode) ────────────────────────────
    int32_t  int_pool[32];
    float    flt_pool[16];
    uint8_t  int_count, flt_count;

    // ── Function table ───────────────────────────────────────────────────
    struct FuncEntry {
        uint16_t code_off;
        uint8_t  param_count;
        uint8_t  local_count;
    };
    FuncEntry funcs[MINIS_MAX_FUNCS];
    uint8_t   func_count;

    // ── Error ────────────────────────────────────────────────────────────
    char err_buf[64];
    bool has_error;

    // ── Helpers ──────────────────────────────────────────────────────────
    void    push(Value v);
    Value   pop();
    Value&  peek();
    Result  exec_one();
    void    vm_abort(const char* msg);

    inline int16_t  operand_i16() const;
    inline uint16_t operand_u16() const;

    const char* get_str(Value v) const;
    uint8_t     alloc_rt_str();
    void        free_rt_str(uint8_t idx);

    uint8_t alloc_arr(uint8_t size);

    Result call_func(uint16_t idx);
    Result return_from(bool has_val);
};
