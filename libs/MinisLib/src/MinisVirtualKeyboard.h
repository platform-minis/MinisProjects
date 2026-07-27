/**
 * MinisVirtualKeyboard.h
 *
 * MyCastle VirtualKeyboard extension for Arduino ESP32-S3.
 * Maintains internal virtual keyboard state (pressed keys, modifier bitmask).
 * No hardware output — state can be polled by GUI automation code on the device.
 *
 * Usage:
 *   #include <MinisIoT.h>
 *   #include <MinisVirtualKeyboard.h>
 *
 *   MinisIoT minis(host, port, userId, deviceId);
 *   MinisVirtualKeyboard kbd(minis);
 *
 *   void setup() { minis.begin(); }
 *
 *   void loop() {
 *     minis.loop();
 *     // read state:
 *     // kbd.isKeyDown("a")   kbd.modifiers()   kbd.lastText()
 *   }
 */

#pragma once

#include "MinisIoT.h"
#include <set>
#include <string>

// Modifier bitmask constants (same layout as USB HID for future compatibility)
constexpr uint8_t VKBD_MOD_LCTRL  = 0x01;
constexpr uint8_t VKBD_MOD_LSHIFT = 0x02;
constexpr uint8_t VKBD_MOD_LALT   = 0x04;
constexpr uint8_t VKBD_MOD_LGUI   = 0x08;
constexpr uint8_t VKBD_MOD_RCTRL  = 0x10;
constexpr uint8_t VKBD_MOD_RSHIFT = 0x20;
constexpr uint8_t VKBD_MOD_RALT   = 0x40;
constexpr uint8_t VKBD_MOD_RGUI   = 0x80;

class MinisVirtualKeyboard {
public:
    static constexpr const char* EXT_TYPE = "vkbd";

    explicit MinisVirtualKeyboard(MinisIoT& minis);

    // ── State accessors ───────────────────────────────────────────────────────
    bool        isKeyDown(const char* key) const;
    uint8_t     modifiers()  const { return _modifiers; }
    const char* lastText()   const { return _lastText.c_str(); }

private:
    MinisIoT&          _minis;
    std::set<std::string> _pressed;   // canonical lowercase key names
    uint8_t            _modifiers = 0;
    std::string        _lastText;

    void _onRequest(const char* id, const char* op, JsonObjectConst params);
    void _respond(const char* id, bool ok,
                  const char* errCode = nullptr, const char* errMsg = nullptr);

    // Returns modifier bitmask for name, or 0 if not a modifier
    static uint8_t _resolveModifier(const char* name);
    // Returns canonical key name (lowercase), or nullptr if unknown
    static const char* _resolveKey(const char* name);
};
