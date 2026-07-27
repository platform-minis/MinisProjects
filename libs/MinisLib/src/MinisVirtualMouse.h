/**
 * MinisVirtualMouse.h
 *
 * MyCastle VirtualMouse extension for Arduino ESP32-S3.
 * Maintains internal virtual mouse state (cursor position, button bitmask).
 * No hardware output — state can be polled by GUI automation code on the device.
 *
 * Usage:
 *   #include <MinisIoT.h>
 *   #include <MinisVirtualMouse.h>
 *
 *   MinisIoT minis(host, port, userId, deviceId);
 *   MinisVirtualMouse mouse(minis);
 *
 *   void setup() { minis.begin(); }
 *
 *   void loop() {
 *     minis.loop();
 *     // read state:
 *     // mouse.x()   mouse.y()   mouse.buttons()   mouse.isPressed("left")
 *   }
 */

#pragma once

#include "MinisIoT.h"

// Button bitmask constants
constexpr uint8_t VMOUSE_BTN_LEFT   = 0x01;
constexpr uint8_t VMOUSE_BTN_RIGHT  = 0x02;
constexpr uint8_t VMOUSE_BTN_MIDDLE = 0x04;

class MinisVirtualMouse {
public:
    static constexpr const char* EXT_TYPE = "vmouse";

    explicit MinisVirtualMouse(MinisIoT& minis);

    // ── State accessors ───────────────────────────────────────────────────────
    int     x()          const { return _x; }
    int     y()          const { return _y; }
    uint8_t buttons()    const { return _buttons; }
    bool    isPressed(const char* button) const;
    int     lastScrollDy() const { return _lastScrollDy; }

private:
    MinisIoT& _minis;
    int     _x = 0;
    int     _y = 0;
    uint8_t _buttons = 0;
    int     _lastScrollDy = 0;

    void _onRequest(const char* id, const char* op, JsonObjectConst params);
    void _respond(const char* id, bool ok,
                  const char* dataJson = nullptr,
                  const char* errCode  = nullptr,
                  const char* errMsg   = nullptr);

    static uint8_t _resolveButton(const char* name);
};
