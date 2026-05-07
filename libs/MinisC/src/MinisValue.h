#pragma once
#include <stdint.h>

enum ValueType : uint8_t {
    T_INT    = 0,
    T_FLOAT  = 1,
    T_BOOL   = 2,
    T_STRING = 3,   // ref: 0x8000 = runtime string flag; low bits = pool index
    T_ARRAY  = 4,   // ref: array pool index (0-based)
    T_NULL   = 5
};

struct Value {
    ValueType type;
    union {
        int32_t  i;     // T_INT
        float    f;     // T_FLOAT
        uint8_t  b;     // T_BOOL (0 or 1)
        uint16_t ref;   // T_STRING, T_ARRAY
    };

    static Value makeInt(int32_t v)    { Value r; r.type=T_INT;   r.i=v;   return r; }
    static Value makeFloat(float v)    { Value r; r.type=T_FLOAT; r.f=v;   return r; }
    static Value makeBool(bool v)      { Value r; r.type=T_BOOL;  r.b=v?1:0; return r; }
    static Value makeStr(uint16_t ref) { Value r; r.type=T_STRING; r.ref=ref; return r; }
    static Value makeArr(uint16_t ref) { Value r; r.type=T_ARRAY;  r.ref=ref; return r; }
    static Value makeNull()            { Value r; r.type=T_NULL;  r.i=0;   return r; }

    bool isTruthy() const {
        switch (type) {
            case T_INT:    return i != 0;
            case T_FLOAT:  return f != 0.0f;
            case T_BOOL:   return b != 0;
            case T_STRING: return true;
            case T_ARRAY:  return true;
            case T_NULL:   return false;
        }
        return false;
    }

    // String ref helpers
    static bool   strIsRuntime(uint16_t ref) { return (ref & 0x8000) != 0; }
    static uint8_t strRtIndex(uint16_t ref)  { return (uint8_t)(ref & 0x00FF); }
    static uint16_t makeRtStrRef(uint8_t i)  { return (uint16_t)(0x8000 | i); }
    static uint16_t makeConstStrRef(uint8_t i) { return (uint16_t)i; }
};
