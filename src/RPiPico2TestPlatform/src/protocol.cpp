#include "protocol.hpp"
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cstdlib>   // strtoul, strtof

namespace protocol {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void str_upper(char* s) {
    while (*s) { *s = (char)toupper((unsigned char)*s); ++s; }
}

// Case-insensitive strcmp
static bool str_ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

// ---------------------------------------------------------------------------
// parse_pin
// ---------------------------------------------------------------------------
bool parse_pin(const char* s, uint8_t& out) {
    if (!s || *s == '\0') return false;

    const char* p = s;
    // Skip optional "GP" or "gp" prefix
    if ((p[0] == 'G' || p[0] == 'g') && (p[1] == 'P' || p[1] == 'p')) {
        p += 2;
    }
    if (*p == '\0') return false;

    // Parse unsigned integer
    char* end = nullptr;
    unsigned long val = strtoul(p, &end, 10);
    if (end == p || *end != '\0') return false;   // non-numeric or trailing chars
    if (val > HAL_MAX_PIN)        return false;   // out of range

    out = (uint8_t)val;
    return true;
}

// ---------------------------------------------------------------------------
// parse_state
// ---------------------------------------------------------------------------
bool parse_state(const char* s, PinState& out) {
    if (!s) return false;
    if (str_ieq(s, "HIGH") || str_ieq(s, "H") || str_ieq(s, "1") || str_ieq(s, "TRUE")) {
        out = PinState::HIGH; return true;
    }
    if (str_ieq(s, "LOW") || str_ieq(s, "L") || str_ieq(s, "0") || str_ieq(s, "FALSE")) {
        out = PinState::LOW; return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// parse_edge
// ---------------------------------------------------------------------------
bool parse_edge(const char* s, EdgeType& out) {
    if (!s) return false;
    if (str_ieq(s, "RISING"))  { out = EdgeType::RISING;  return true; }
    if (str_ieq(s, "FALLING")) { out = EdgeType::FALLING; return true; }
    if (str_ieq(s, "BOTH"))    { out = EdgeType::BOTH;    return true; }
    return false;
}

// ---------------------------------------------------------------------------
// parse_uint
// ---------------------------------------------------------------------------
bool parse_uint(const char* s, uint32_t min_val, uint32_t max_val, uint32_t& out) {
    if (!s || *s == '\0') return false;
    char* end = nullptr;
    unsigned long val = strtoul(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (val < min_val || val > max_val) return false;
    out = (uint32_t)val;
    return true;
}

// ---------------------------------------------------------------------------
// parse_float
// ---------------------------------------------------------------------------
bool parse_float(const char* s, float min_val, float max_val, float& out) {
    if (!s || *s == '\0') return false;
    char* end = nullptr;
    float val = strtof(s, &end);
    if (end == s || *end != '\0') return false;
    if (val < min_val || val > max_val) return false;
    out = val;
    return true;
}

// ---------------------------------------------------------------------------
// parse_seq_step  "GP<n>:HIGH|LOW[:<delay_us>]"
// ---------------------------------------------------------------------------
bool parse_seq_step(const char* token, SeqStep& out) {
    if (!token) return false;

    // Copy token so we can modify it
    char buf[64];
    size_t len = strlen(token);
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, token, len + 1);

    // Split on ':'
    char* part[3] = {nullptr, nullptr, nullptr};
    int n = 0;
    part[n++] = buf;
    for (char* p = buf; *p; ++p) {
        if (*p == ':') {
            *p = '\0';
            if (n >= 3) return false;  // too many colons
            part[n++] = p + 1;
        }
    }
    if (n < 2) return false;  // need at least pin:state

    if (!parse_pin(part[0], out.pin))     return false;
    if (!parse_state(part[1], out.state)) return false;

    out.delay_after_us = 0;
    if (n == 3) {
        if (!parse_uint(part[2], 0, HAL_PULSE_US_MAX, out.delay_after_us)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// tokenise
// ---------------------------------------------------------------------------
bool tokenise(char* buf, ParsedCommand& cmd) {
    if (!buf) return false;

    // Strip trailing \r\n
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n')) {
        buf[--len] = '\0';
    }
    if (len == 0 || len > MAX_LINE_LEN) return false;

    cmd.argc = 0;

    // First token = command name
    char* p = buf;
    // Skip leading whitespace
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') return false;

    cmd.name[0] = '\0';
    size_t ni = 0;
    while (*p && *p != ' ' && *p != '\t') {
        if (ni < sizeof(cmd.name) - 1) {
            cmd.name[ni++] = (char)toupper((unsigned char)*p);
        }
        ++p;
    }
    cmd.name[ni] = '\0';

    // Remaining tokens = arguments
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        if (cmd.argc >= MAX_SEQ_STEPS + 4) return false;
        cmd.args[cmd.argc++] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        if (*p) { *p = '\0'; ++p; }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Response formatters
// ---------------------------------------------------------------------------
size_t fmt_ok(char* buf, size_t buf_size) {
    return (size_t)snprintf(buf, buf_size, "OK\n");
}

size_t fmt_ok_str(char* buf, size_t buf_size, const char* data) {
    return (size_t)snprintf(buf, buf_size, "OK:%s\n", data ? data : "");
}

size_t fmt_err(char* buf, size_t buf_size, const char* code, const char* msg) {
    return (size_t)snprintf(buf, buf_size, "ERR:%s:%s\n",
                            code ? code : "UNKNOWN",
                            msg  ? msg  : "");
}

size_t fmt_data_raw(char* buf, size_t buf_size, const char* json) {
    return (size_t)snprintf(buf, buf_size, "DATA:%s\n", json ? json : "{}");
}

} // namespace protocol
