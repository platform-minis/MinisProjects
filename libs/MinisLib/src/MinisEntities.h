/**
 * MinisEntities.h — IotEntity helpers for the MinisIoT Arduino library
 *
 * Mirrors the TypeScript IotEntity types from
 * packages/core/src/models/IotModels.ts and the MicroPython
 * minis_entities.py in libs/uMinisLib.
 *
 * Entities are registered via MinisIoT::addEntity() and announced in
 * the hello message so MyCastle renders the correct UI controls.
 *
 * Writable entity types (MinisSwitch, MinisNumber, MinisButton, MinisSelect)
 * handle incoming commands whose name matches the entity id and are
 * auto-acknowledged — no manual ackCommand() needed.
 *
 * Read-only entity types (MinisSensor, MinisBinarySensor) are metadata
 * declarations; their values must be published via
 * MinisIoT::sendTelemetry() using the entity id as the metric key.
 */

#pragma once

#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <string>

// ─── Base entity ──────────────────────────────────────────────────────────────

/**
 * Base class for all IoT entities. Not used directly.
 *
 * Override handleCommand() in writable subclasses.
 * Override toJsonObject() to append type-specific fields.
 */
class MinisEntity {
public:
    MinisEntity(const char* id, const char* type, const char* name,
                const char* icon = nullptr, const char* deviceClass = nullptr)
        : _id(id), _type(type), _name(name)
    {
        if (icon)        _icon        = icon;
        if (deviceClass) _deviceClass = deviceClass;
    }
    virtual ~MinisEntity() = default;

    const char* id()   const { return _id.c_str(); }
    const char* type() const { return _type.c_str(); }

    /** Serialize to the JSON object that goes inside the hello "entities" array. */
    virtual void toJsonObject(JsonObject obj) const {
        obj["id"]   = _id.c_str();
        obj["type"] = _type.c_str();
        obj["name"] = _name.c_str();
        if (!_icon.empty())        obj["icon"]        = _icon.c_str();
        if (!_deviceClass.empty()) obj["deviceClass"] = _deviceClass.c_str();
    }

    /**
     * Called by MinisIoT when a command whose name equals this entity's id
     * arrives. Default implementation is a no-op (read-only entities).
     */
    virtual void handleCommand(JsonObjectConst /*payload*/) {}

protected:
    std::string _id;
    std::string _type;
    std::string _name;
    std::string _icon;
    std::string _deviceClass;
};

// ─── Read-only entities ───────────────────────────────────────────────────────

/**
 * Read-only numeric sensor.
 *
 * Report value via MinisIoT::sendTelemetry() using entity id as metric key.
 *
 * @param id          Unique id — must match the telemetry metric key.
 * @param name        Human-readable label shown in MyCastle.
 * @param unit        Unit string, e.g. "%", "°C", "MB".
 * @param icon        Optional icon name.
 * @param deviceClass Optional HA-style class, e.g. "power_factor".
 */
class MinisSensor : public MinisEntity {
public:
    MinisSensor(const char* id, const char* name,
                const char* unit = "",
                const char* icon = nullptr,
                const char* deviceClass = nullptr)
        : MinisEntity(id, "sensor", name, icon, deviceClass), _unit(unit ? unit : "")
    {}

    void toJsonObject(JsonObject obj) const override {
        MinisEntity::toJsonObject(obj);
        obj["unit"] = _unit.c_str();
    }

private:
    std::string _unit;
};

/**
 * Read-only boolean sensor.
 *
 * Report value via MinisIoT::sendTelemetry() using entity id as metric key.
 *
 * @param id          Unique id — must match the telemetry metric key.
 * @param name        Human-readable label.
 * @param onLabel     Text shown when true.  nullptr = omit.
 * @param offLabel    Text shown when false. nullptr = omit.
 * @param icon        Optional icon name.
 * @param deviceClass Optional HA-style class, e.g. "motion".
 */
class MinisBinarySensor : public MinisEntity {
public:
    MinisBinarySensor(const char* id, const char* name,
                      const char* onLabel  = nullptr,
                      const char* offLabel = nullptr,
                      const char* icon     = nullptr,
                      const char* deviceClass = nullptr)
        : MinisEntity(id, "binary_sensor", name, icon, deviceClass)
    {
        if (onLabel)  _onLabel  = onLabel;
        if (offLabel) _offLabel = offLabel;
    }

    void toJsonObject(JsonObject obj) const override {
        MinisEntity::toJsonObject(obj);
        if (!_onLabel.empty())  obj["onLabel"]  = _onLabel.c_str();
        if (!_offLabel.empty()) obj["offLabel"] = _offLabel.c_str();
    }

private:
    std::string _onLabel;
    std::string _offLabel;
};

// ─── Writable entities ────────────────────────────────────────────────────────

/**
 * Writable boolean toggle.
 *
 * Command payload: {"state": true | false}
 * Callback signature: callback(bool state)
 */
class MinisSwitch : public MinisEntity {
public:
    using Callback = std::function<void(bool state)>;

    MinisSwitch(const char* id, const char* name,
                Callback callback = nullptr,
                const char* icon = nullptr,
                const char* deviceClass = nullptr)
        : MinisEntity(id, "switch", name, icon, deviceClass), _cb(std::move(callback))
    {}

    void handleCommand(JsonObjectConst payload) override {
        if (_cb && payload.containsKey("state")) {
            _cb(payload["state"].as<bool>());
        }
    }

private:
    Callback _cb;
};

/**
 * Writable numeric value with min/max/step constraints.
 *
 * Command payload: {"value": <number>}
 * Callback signature: callback(float value)
 */
class MinisNumber : public MinisEntity {
public:
    using Callback = std::function<void(float value)>;

    MinisNumber(const char* id, const char* name,
                float minVal, float maxVal, float step,
                const char* unit     = nullptr,
                Callback    callback = nullptr,
                const char* icon     = nullptr,
                const char* deviceClass = nullptr)
        : MinisEntity(id, "number", name, icon, deviceClass)
        , _min(minVal), _max(maxVal), _step(step)
        , _cb(std::move(callback))
    {
        if (unit) _unit = unit;
    }

    void toJsonObject(JsonObject obj) const override {
        MinisEntity::toJsonObject(obj);
        obj["min"]  = _min;
        obj["max"]  = _max;
        obj["step"] = _step;
        if (!_unit.empty()) obj["unit"] = _unit.c_str();
    }

    void handleCommand(JsonObjectConst payload) override {
        if (_cb && payload.containsKey("value")) {
            _cb(payload["value"].as<float>());
        }
    }

private:
    float       _min, _max, _step;
    std::string _unit;
    Callback    _cb;
};

/**
 * Writable momentary button.
 *
 * Command payload: {} (empty)
 * Callback signature: callback()
 */
class MinisButton : public MinisEntity {
public:
    using Callback = std::function<void()>;

    MinisButton(const char* id, const char* name,
                Callback callback = nullptr,
                const char* icon = nullptr,
                const char* deviceClass = nullptr)
        : MinisEntity(id, "button", name, icon, deviceClass), _cb(std::move(callback))
    {}

    void handleCommand(JsonObjectConst /*payload*/) override {
        if (_cb) _cb();
    }

private:
    Callback _cb;
};

/**
 * Writable enum selector.
 *
 * Command payload: {"value": "<option>"} or {"option": "<option>"}
 * Callback signature: callback(const char* option)
 * Unknown option values are silently ignored.
 */
class MinisSelect : public MinisEntity {
public:
    using Callback = std::function<void(const char* option)>;

    MinisSelect(const char* id, const char* name,
                std::vector<std::string> options,
                Callback callback = nullptr,
                const char* icon = nullptr,
                const char* deviceClass = nullptr)
        : MinisEntity(id, "select", name, icon, deviceClass)
        , _options(std::move(options))
        , _cb(std::move(callback))
    {}

    void toJsonObject(JsonObject obj) const override {
        MinisEntity::toJsonObject(obj);
        JsonArray arr = obj["options"].to<JsonArray>();
        for (const auto& opt : _options) {
            arr.add(opt.c_str());
        }
    }

    void handleCommand(JsonObjectConst payload) override {
        if (!_cb) return;
        const char* val = nullptr;
        if (payload.containsKey("value"))  val = payload["value"].as<const char*>();
        if (!val && payload.containsKey("option")) val = payload["option"].as<const char*>();
        if (!val || !val[0]) return;
        for (const auto& opt : _options) {
            if (opt == val) {
                _cb(val);
                return;
            }
        }
        // unknown option — silently ignore
    }

private:
    std::vector<std::string> _options;
    Callback                 _cb;
};
