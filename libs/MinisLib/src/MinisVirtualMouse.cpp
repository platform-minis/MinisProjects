/**
 * MinisVirtualMouse.cpp
 */

#include "MinisVirtualMouse.h"
#include <cstring>

// ── Button name → bitmask ─────────────────────────────────────────────────────

uint8_t MinisVirtualMouse::_resolveButton(const char* name) {
    if (!name || strcasecmp(name, "left")   == 0) return VMOUSE_BTN_LEFT;
    if (         strcasecmp(name, "right")  == 0) return VMOUSE_BTN_RIGHT;
    if (         strcasecmp(name, "middle") == 0) return VMOUSE_BTN_MIDDLE;
    return VMOUSE_BTN_LEFT;
}

// ── Constructor ───────────────────────────────────────────────────────────────

MinisVirtualMouse::MinisVirtualMouse(MinisIoT& minis) : _minis(minis) {
    minis.addExtension(EXT_TYPE, [this](const char* id, const char* op, JsonObjectConst params) {
        _onRequest(id, op, params);
    });
}

// ── State accessor ────────────────────────────────────────────────────────────

bool MinisVirtualMouse::isPressed(const char* button) const {
    return (_buttons & _resolveButton(button)) != 0;
}

// ── Request handler ───────────────────────────────────────────────────────────

void MinisVirtualMouse::_onRequest(const char* id, const char* op, JsonObjectConst params) {

    if (strcmp(op, "move") == 0) {
        _x = params["x"].as<int>();
        _y = params["y"].as<int>();

    } else if (strcmp(op, "move_rel") == 0) {
        _x += params["dx"].as<int>();
        _y += params["dy"].as<int>();

    } else if (strcmp(op, "click") == 0) {
        if (params["x"].is<int>() && params["y"].is<int>()) {
            _x = params["x"].as<int>();
            _y = params["y"].as<int>();
        }
        // state: no persistent button change for a click

    } else if (strcmp(op, "double_click") == 0) {
        if (params["x"].is<int>() && params["y"].is<int>()) {
            _x = params["x"].as<int>();
            _y = params["y"].as<int>();
        }

    } else if (strcmp(op, "press") == 0) {
        _buttons |= _resolveButton(params["button"] | (const char*)nullptr);

    } else if (strcmp(op, "release") == 0) {
        _buttons &= ~_resolveButton(params["button"] | (const char*)nullptr);

    } else if (strcmp(op, "scroll") == 0) {
        _lastScrollDy = params["dy"].as<int>();

    } else if (strcmp(op, "drag") == 0) {
        uint8_t btn = _resolveButton(params["button"] | (const char*)nullptr);
        _x = params["x1"].as<int>();
        _y = params["y1"].as<int>();
        _buttons |= btn;
        _x = params["x2"].as<int>();
        _y = params["y2"].as<int>();
        _buttons &= ~btn;

    } else if (strcmp(op, "get_pos") == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "{\"x\":%d,\"y\":%d}", _x, _y);
        _respond(id, true, buf);
        return;

    } else {
        _respond(id, false, nullptr, "Error", "Unknown op");
        return;
    }

    _respond(id, true);
}

// ── Respond helper ────────────────────────────────────────────────────────────

void MinisVirtualMouse::_respond(const char* id, bool ok,
                                  const char* dataJson,
                                  const char* errCode,
                                  const char* errMsg) {
    _minis.extRespond(EXT_TYPE, id, ok, dataJson, errCode, errMsg);
}
