#pragma once
/**
 * Hydra — encje urządzenia w MyCastle.
 *
 * Encja to deklaracja: „mam czujnik temperatury", „mam przełącznik". Panel
 * MyCastle rysuje z niej odpowiedni element interfejsu, nie pytając o nic
 * więcej. Deklaracje idą w wiadomości `hello`, więc urządzenie opisuje się
 * samo — dodanie przekaźnika nie wymaga zmiany po stronie serwera.
 *
 * Struktura, nie hierarchia klas. MinisLib ma tu klasę bazową i sześć
 * pochodnych z metodą wirtualną; tutaj jeden typ z polem `kind` i wytwórniami.
 * Powód jest praktyczny: sześć klas z wirtualnym `toJson` to sześć tablic
 * metod wirtualnych i zakaz `constexpr`, a pola i tak są prawie te same.
 * Encje da się dzięki temu zadeklarować jako `constexpr` obok kodu, który
 * ich używa.
 *
 *     Entity gTemp   = Entity::sensor("temp", "Temperatura", "temperature", "°C");
 *     Entity gRelay  = Entity::toggle("relay", "Przekaźnik",
 *                                     [](bool on) { relay.set(on); return true; });
 *
 * Encje **zapisywalne** (przełącznik, liczba, przycisk, wybór) przechwytują
 * komendę o nazwie równej swojemu identyfikatorowi i same ją potwierdzają.
 * Encje **tylko do odczytu** (czujniki) są wyłącznie deklaracją — wartości
 * wysyła się telemetrią, używając identyfikatora encji jako klucza pomiaru.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/Delegate.hpp"
#include "hydra/util/Json.hpp"

namespace hydra {
namespace minis {

enum class EntityKind : u8 {
    Sensor = 0,      ///< liczba tylko do odczytu
    BinarySensor,    ///< stan dwuwartościowy tylko do odczytu
    Switch,          ///< przełącznik
    Number,          ///< liczba nastawna
    Button,          ///< przycisk bez stanu
    Select,          ///< wybór z listy
};

constexpr const char* toString(EntityKind kind) {
    switch (kind) {
        case EntityKind::Sensor:       return "sensor";
        case EntityKind::BinarySensor: return "binary_sensor";
        case EntityKind::Switch:       return "switch";
        case EntityKind::Number:       return "number";
        case EntityKind::Button:       return "button";
        case EntityKind::Select:       return "select";
    }
    return "sensor";
}

struct Entity {
    /** Identyfikator — zarazem klucz pomiaru i nazwa komendy zapisu. */
    const char* id   = nullptr;
    const char* name = nullptr;
    EntityKind  kind = EntityKind::Sensor;

    /** Klasa urządzenia w rozumieniu panelu: „temperature", „humidity", „motion". */
    const char* deviceClass = nullptr;
    /** Jednostka pomiaru; ignorowana dla encji nieliczbowych. */
    const char* unit = nullptr;

    // --- tylko dla Number ---------------------------------------------------
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step    = 0.0f;

    // --- tylko dla Select ---------------------------------------------------
    const char* const* options     = nullptr;
    u8                 optionCount = 0;

    /**
     * Zapis wartości z panelu. Zwraca `false`, gdy się nie udało — moduł
     * odpowie wtedy `FAILED` zamiast `ACKNOWLEDGED`, a panel to pokaże.
     *
     * Wartość jest widokiem JSON, bo każdy rodzaj encji ma inny typ: logiczny
     * dla przełącznika, liczbowy dla nastawy, tekstowy dla wyboru.
     */
    using WriteHandler = Delegate<bool(json::JsonView value)>;
    WriteHandler onWrite{};

    bool writable() const {
        return kind == EntityKind::Switch || kind == EntityKind::Number ||
               kind == EntityKind::Button || kind == EntityKind::Select;
    }

    /** Dopisuje opis encji jako obiekt w bieżącej tablicy JSON. */
    void describe(json::JsonWriter& out) const;

    // --- wytwórnie ----------------------------------------------------------

    static Entity sensor(const char* id, const char* name,
                         const char* deviceClass = nullptr, const char* unit = nullptr) {
        Entity e;
        e.id = id; e.name = name; e.kind = EntityKind::Sensor;
        e.deviceClass = deviceClass; e.unit = unit;
        return e;
    }

    static Entity binarySensor(const char* id, const char* name,
                               const char* deviceClass = nullptr) {
        Entity e;
        e.id = id; e.name = name; e.kind = EntityKind::BinarySensor;
        e.deviceClass = deviceClass;
        return e;
    }

    static Entity toggle(const char* id, const char* name, WriteHandler onWrite) {
        Entity e;
        e.id = id; e.name = name; e.kind = EntityKind::Switch; e.onWrite = onWrite;
        return e;
    }

    static Entity number(const char* id, const char* name,
                         float minimum, float maximum, float step,
                         WriteHandler onWrite, const char* unit = nullptr) {
        Entity e;
        e.id = id; e.name = name; e.kind = EntityKind::Number;
        e.minimum = minimum; e.maximum = maximum; e.step = step;
        e.unit = unit; e.onWrite = onWrite;
        return e;
    }

    static Entity button(const char* id, const char* name, WriteHandler onWrite) {
        Entity e;
        e.id = id; e.name = name; e.kind = EntityKind::Button; e.onWrite = onWrite;
        return e;
    }

    static Entity select(const char* id, const char* name,
                         const char* const* options, u8 count, WriteHandler onWrite) {
        Entity e;
        e.id = id; e.name = name; e.kind = EntityKind::Select;
        e.options = options; e.optionCount = count; e.onWrite = onWrite;
        return e;
    }
};

/** Pojedynczy pomiar wysyłany telemetrią. */
struct Metric {
    enum class Type : u8 { Float, Bool, Text };

    const char* key  = nullptr;
    const char* unit = nullptr;
    Type        type = Type::Float;
    float       number = 0.0f;
    bool        flag   = false;
    const char* text   = nullptr;

    static Metric of(const char* key, float value, const char* unit = nullptr) {
        Metric m; m.key = key; m.type = Type::Float; m.number = value; m.unit = unit;
        return m;
    }
    static Metric of(const char* key, bool value) {
        Metric m; m.key = key; m.type = Type::Bool; m.flag = value;
        return m;
    }
    static Metric of(const char* key, const char* value) {
        Metric m; m.key = key; m.type = Type::Text; m.text = value;
        return m;
    }
};

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
