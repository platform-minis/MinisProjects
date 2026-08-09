#pragma once
/**
 * Hydra — JSON bez alokacji, w obie strony.
 *
 * Powstał dla modułu `minis`, bo protokół MyCastle jest w JSON-ie, a żadna
 * z dostępnych bibliotek nie pasuje do reguł frameworka: ArduinoJson alokuje
 * i wymaga Arduino, nlohmann wymaga wyjątków i STL-a. Zakres jest więc
 * dokładnie taki, jaki wynika z protokołu, i ani jednej rzeczy więcej.
 *
 * **Zapis** (`JsonWriter`) — strumieniowo do bufora wołającego. Nie buduje
 * drzewa, bo drzewo trzeba by gdzieś trzymać. Przepełnienie bufora nie jest
 * błędem zgłaszanym po fakcie: pisarz przechodzi w stan błędu i zostaje w nim,
 * więc sprawdza się go raz, na końcu.
 *
 * **Odczyt** (`JsonView`) — skaner po surowym tekście, bez kopiowania. Szuka
 * klucza i zwraca wartość jako widok na oryginalny bufor. Nie parsuje całości
 * z góry, więc odczyt jednego pola z dużego dokumentu kosztuje tyle, ile
 * przejście do tego pola.
 *
 *     u8 buf[256];
 *     JsonWriter w{ByteSpan{buf, sizeof(buf)}};
 *     w.beginObject();
 *       w.key("id").value("abc");
 *       w.key("ok").value(true);
 *       w.key("metrics").beginArray();
 *         w.beginObject().key("key").value("temp").key("value").value(21.5f).endObject();
 *       w.endArray();
 *     w.endObject();
 *     if (!w.ok()) { … }
 *
 *     JsonView v{w.text()};
 *     bool ok = false;
 *     v.get("ok").asBool(ok);
 */

#include "hydra/core/Config.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace json {

// ---------------------------------------------------------------------------
// Zapis
// ---------------------------------------------------------------------------

class JsonWriter {
public:
    explicit JsonWriter(ByteSpan out) : out_(out) {}

    JsonWriter& beginObject();
    JsonWriter& endObject();
    JsonWriter& beginArray();
    JsonWriter& endArray();

    /** Nazwa pola. Kolejne wywołanie `value()` zapisuje jego wartość. */
    JsonWriter& key(const char* name);

    JsonWriter& value(const char* text);
    /** Napis o znanej długości — dla fragmentów bez zera kończącego. */
    JsonWriter& value(const char* text, size_t length);
    JsonWriter& value(bool v);
    JsonWriter& value(i32 v);
    JsonWriter& value(u32 v);
    JsonWriter& value(float v);
    JsonWriter& valueNull();

    /**
     * Wstawia gotowy fragment JSON-a bez cytowania.
     *
     * Potrzebne w jednym miejscu: rozszerzenie oddaje wynik jako gotowy
     * dokument, a opakowanie go w napis dałoby na serwerze zagnieżdżony
     * tekst zamiast obiektu. Treść nie jest sprawdzana — to jedyne miejsce,
     * w którym pisarz ufa wołającemu.
     */
    JsonWriter& raw(const char* fragment);

    bool   ok() const { return ok_; }
    size_t length() const { return len_; }
    /** Zapisany tekst z zerem kończącym; pusty napis, gdy pisarz jest w błędzie. */
    const char* text() const;

    /** Ile bajtów by wyszło — także wtedy, gdy bufor był za mały. */
    size_t needed() const { return needed_; }

private:
    void put(char c);
    void put(const char* s);
    void put(const char* s, size_t n);
    /** Napis w cudzysłowie z ucieczkami wymaganymi przez RFC 8259. */
    void quoted(const char* s, size_t n);
    void separate();

    ByteSpan out_;
    size_t   len_    = 0;
    size_t   needed_ = 0;
    bool     ok_     = true;
    /** Czy w bieżącym kontenerze coś już jest — decyduje o przecinku. */
    bool     dirty_  = false;
    /** Po `key()` przecinka nie stawiamy, bo dwukropek już rozdzielił. */
    bool     afterKey_ = false;
};

// ---------------------------------------------------------------------------
// Odczyt
// ---------------------------------------------------------------------------

enum class JsonType : u8 { Invalid = 0, Null, Bool, Number, String, Object, Array };

/**
 * Widok na wartość JSON leżącą w cudzym buforze.
 *
 * Nie posiada danych. Bufor musi żyć tak długo, jak widok — w praktyce oba
 * są lokalne w obsłudze jednej wiadomości, więc to naturalne.
 */
class JsonView {
public:
    JsonView() = default;
    JsonView(const char* text, size_t length) : p_(text), n_(length) {}
    explicit JsonView(const char* text);

    JsonType type() const;
    bool     valid() const { return p_ != nullptr && n_ > 0; }

    /** Pole obiektu. Nieistniejące daje widok nieważny, nie błąd. */
    JsonView get(const char* key) const;
    /** Element tablicy. */
    JsonView at(size_t index) const;
    /** Liczba elementów tablicy albo pól obiektu. */
    size_t   size() const;

    /**
     * Napis skopiowany do bufora wołającego, z rozwiniętymi ucieczkami.
     * Zwraca `false`, gdy wartość nie jest napisem albo się nie mieści.
     */
    bool asString(char* out, size_t capacity) const;
    bool asBool(bool& out) const;
    bool asInt(i32& out) const;
    bool asFloat(float& out) const;

    /** Napis albo `fallback` — do miejsc, w których brak pola jest normalny. */
    const char* asStringOr(char* out, size_t capacity, const char* fallback) const;

    /** Surowy tekst wartości — dla obiektów i tablic przekazywanych dalej. */
    const char* raw() const { return p_; }
    size_t      rawLength() const { return n_; }

private:
    /** Pierwszy znak niebędący białym, licząc od `i`. */
    static size_t skipWs(const char* p, size_t n, size_t i);
    /** Indeks tuż za wartością zaczynającą się na `i`; `n`, gdy niepoprawna. */
    static size_t skipValue(const char* p, size_t n, size_t i);

    const char* p_ = nullptr;
    size_t      n_ = 0;
};

}  // namespace json
}  // namespace hydra
