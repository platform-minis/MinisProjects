#pragma once
#include "MinisValue.h"

// Signature for all native functions callable from MinisC scripts.
// args[0] = first argument (left-most), args[argc-1] = last.
// Return Value::makeNull() for void functions.
typedef Value (*NativeFn)(Value* args, uint8_t argc);

struct NativeDef {
    const char* name;
    NativeFn    fn;
    uint8_t     argc;       // exact argument count expected
    ValueType   ret_type;   // T_NULL = void (no return value pushed)
};
