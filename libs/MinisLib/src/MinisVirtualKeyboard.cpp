/**
 * MinisVirtualKeyboard.cpp
 */

#include "MinisVirtualKeyboard.h"
#include <cstring>
#include <cctype>
#include <algorithm>

// ── Key / modifier tables ─────────────────────────────────────────────────────

static const struct { const char* name; uint8_t bit; } MOD_TABLE[] = {
    { "ctrl",   VKBD_MOD_LCTRL  }, { "lctrl",  VKBD_MOD_LCTRL  }, { "rctrl",  VKBD_MOD_RCTRL  },
    { "shift",  VKBD_MOD_LSHIFT }, { "lshift", VKBD_MOD_LSHIFT }, { "rshift", VKBD_MOD_RSHIFT },
    { "alt",    VKBD_MOD_LALT   }, { "lalt",   VKBD_MOD_LALT   }, { "ralt",   VKBD_MOD_RALT   },
    { "win",    VKBD_MOD_LGUI   }, { "gui",    VKBD_MOD_LGUI   }, { "cmd",    VKBD_MOD_LGUI   },
};

// Named special keys (single-char keys are handled separately)
static const char* NAMED_KEYS[] = {
    "enter", "return", "esc", "escape", "backspace", "tab", "space",
    "delete", "home", "end", "pageup", "pagedown",
    "up", "down", "left", "right",
    "f1","f2","f3","f4","f5","f6","f7","f8","f9","f10","f11","f12",
};

// ── Static helpers ────────────────────────────────────────────────────────────

uint8_t MinisVirtualKeyboard::_resolveModifier(const char* name) {
    for (const auto& e : MOD_TABLE) {
        if (strcasecmp(e.name, name) == 0) return e.bit;
    }
    return 0;
}

const char* MinisVirtualKeyboard::_resolveKey(const char* name) {
    if (!name || name[0] == '\0') return nullptr;
    // Single printable ASCII character
    if (name[1] == '\0' && isprint((unsigned char)name[0])) return name;
    // Named key
    for (const char* k : NAMED_KEYS) {
        if (strcasecmp(k, name) == 0) return k;
    }
    return nullptr;
}

// ── Constructor ───────────────────────────────────────────────────────────────

MinisVirtualKeyboard::MinisVirtualKeyboard(MinisIoT& minis) : _minis(minis) {
    minis.addExtension(EXT_TYPE, [this](const char* id, const char* op, JsonObjectConst params) {
        _onRequest(id, op, params);
    });
}

// ── State accessor ────────────────────────────────────────────────────────────

bool MinisVirtualKeyboard::isKeyDown(const char* key) const {
    if (!key) return false;
    char lower[32];
    size_t i = 0;
    for (; key[i] && i < sizeof(lower) - 1; ++i)
        lower[i] = (char)tolower((unsigned char)key[i]);
    lower[i] = '\0';
    return _pressed.count(lower) > 0;
}

// ── Request handler ───────────────────────────────────────────────────────────

void MinisVirtualKeyboard::_onRequest(const char* id, const char* op, JsonObjectConst params) {

    if (strcmp(op, "key_press") == 0) {
        const char* key = params["key"] | "";
        if (!_resolveKey(key)) { _respond(id, false, "Error", "Unknown key"); return; }
        // Press modifiers
        uint8_t modByte = 0;
        JsonArrayConst mods = params["modifiers"].as<JsonArrayConst>();
        for (JsonVariantConst m : mods) modByte |= _resolveModifier(m | "");
        _modifiers |= modByte;
        _pressed.insert(key);
        // Simulate release
        _pressed.erase(key);
        _modifiers &= ~modByte;

    } else if (strcmp(op, "key_down") == 0) {
        const char* key = params["key"] | "";
        uint8_t mod = _resolveModifier(key);
        if (mod) {
            _modifiers |= mod;
        } else {
            if (!_resolveKey(key)) { _respond(id, false, "Error", "Unknown key"); return; }
            _pressed.insert(key);
        }

    } else if (strcmp(op, "key_up") == 0) {
        const char* key = params["key"] | "";
        uint8_t mod = _resolveModifier(key);
        if (mod) {
            _modifiers &= ~mod;
        } else if (_resolveKey(key)) {
            _pressed.erase(key);
        } else {
            _pressed.clear();
            _modifiers = 0;
        }

    } else if (strcmp(op, "type_text") == 0) {
        _lastText = params["text"] | "";

    } else if (strcmp(op, "hotkey") == 0) {
        JsonArrayConst keys = params["keys"].as<JsonArrayConst>();
        uint8_t modByte = 0;
        for (JsonVariantConst k : keys) {
            const char* ks = k | "";
            uint8_t mod = _resolveModifier(ks);
            if (mod) {
                modByte |= mod;
            } else if (_resolveKey(ks)) {
                _pressed.insert(ks);
            }
        }
        _modifiers |= modByte;
        // Release all
        _pressed.clear();
        _modifiers = 0;

    } else {
        _respond(id, false, "Error", "Unknown op");
        return;
    }

    _respond(id, true);
}

// ── Respond helper ────────────────────────────────────────────────────────────

void MinisVirtualKeyboard::_respond(const char* id, bool ok,
                                     const char* errCode, const char* errMsg) {
    _minis.extRespond(EXT_TYPE, id, ok, nullptr, errCode, errMsg);
}
