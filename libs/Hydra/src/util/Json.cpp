/** Hydra — implementacja JSON-a bez alokacji. */

#include "hydra/util/Json.hpp"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace hydra {
namespace json {

// ---------------------------------------------------------------------------
// Zapis
// ---------------------------------------------------------------------------

void JsonWriter::put(char c) {
    ++needed_;
    // Jeden bajt zostaje na zero kończące — inaczej text() musiałby ucinać
    // ostatni znak, a wołający dostałby napis krótszy, niż zgłasza length().
    if (!ok_ || len_ + 1 >= out_.size()) { ok_ = false; return; }
    out_.data()[len_++] = static_cast<u8>(c);
}

void JsonWriter::put(const char* s) { put(s, strlen(s)); }

void JsonWriter::put(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) put(s[i]);
}

void JsonWriter::separate() {
    if (afterKey_) { afterKey_ = false; return; }
    if (dirty_) put(',');
}

/**
 * Ucieczki wymagane przez RFC 8259.
 *
 * Znaki sterujące poniżej 0x20 muszą wyjść jako \uXXXX — inaczej serwer
 * odrzuci cały dokument. Bajty powyżej 0x7F przepuszczamy bez zmian: to
 * poprawne UTF-8 wchodzące z zewnątrz, a przekodowywanie go tutaj tylko
 * psułoby polskie znaki w nazwach encji.
 */
void JsonWriter::quoted(const char* s, size_t n) {
    put('"');
    for (size_t i = 0; i < n; ++i) {
        const char c = s[i];
        switch (c) {
            case '"':  put("\\\""); break;
            case '\\': put("\\\\"); break;
            case '\n': put("\\n");  break;
            case '\r': put("\\r");  break;
            case '\t': put("\\t");  break;
            case '\b': put("\\b");  break;
            case '\f': put("\\f");  break;
            default:
                if (static_cast<u8>(c) < 0x20) {
                    char esc[7];
                    snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c) & 0xFF);
                    put(esc);
                } else {
                    put(c);
                }
        }
    }
    put('"');
}

JsonWriter& JsonWriter::beginObject() { separate(); put('{'); dirty_ = false; return *this; }
JsonWriter& JsonWriter::beginArray()  { separate(); put('['); dirty_ = false; return *this; }

JsonWriter& JsonWriter::endObject() { put('}'); dirty_ = true; return *this; }
JsonWriter& JsonWriter::endArray()  { put(']'); dirty_ = true; return *this; }

JsonWriter& JsonWriter::key(const char* name) {
    separate();
    quoted(name, strlen(name));
    put(':');
    afterKey_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(const char* text) {
    return value(text, text ? strlen(text) : 0);
}

JsonWriter& JsonWriter::value(const char* text, size_t length) {
    separate();
    if (text == nullptr) put("null");
    else                 quoted(text, length);
    dirty_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(bool v) {
    separate();
    put(v ? "true" : "false");
    dirty_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(i32 v) {
    separate();
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%ld", static_cast<long>(v));
    put(tmp);
    dirty_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(u32 v) {
    separate();
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%lu", static_cast<unsigned long>(v));
    put(tmp);
    dirty_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(float v) {
    separate();
    // NaN i nieskończoność nie istnieją w JSON-ie. Wypisanie ich literalnie
    // dawałoby dokument, który serwer odrzuca w całości — razem z pozostałymi
    // pomiarami. Brakujący odczyt to `null`, a nie zepsuta paczka.
    if (isnan(v) || isinf(v)) { put("null"); dirty_ = true; return *this; }

    char tmp[20];
    // %g zamiast %f: 21.5 ma wyjść jako „21.5", a nie „21.500000" — na łączu
    // 9600 bodów pięć zbędnych znaków na pomiar to realny koszt.
    snprintf(tmp, sizeof(tmp), "%g", static_cast<double>(v));
    put(tmp);
    dirty_ = true;
    return *this;
}

JsonWriter& JsonWriter::valueNull() {
    separate();
    put("null");
    dirty_ = true;
    return *this;
}

JsonWriter& JsonWriter::raw(const char* fragment) {
    separate();
    put(fragment);
    dirty_ = true;
    return *this;
}

const char* JsonWriter::text() const {
    if (out_.size() == 0) return "";
    out_.data()[ok_ ? len_ : 0] = 0;
    return reinterpret_cast<const char*>(out_.data());
}

// ---------------------------------------------------------------------------
// Odczyt
// ---------------------------------------------------------------------------

JsonView::JsonView(const char* text) : p_(text), n_(text ? strlen(text) : 0) {}

size_t JsonView::skipWs(const char* p, size_t n, size_t i) {
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r')) ++i;
    return i;
}

/**
 * Przeskakuje jedną wartość.
 *
 * Zliczanie nawiasów musi rozumieć napisy i ucieczki — bez tego `{"a":"}"}`
 * kończy obiekt w środku napisu. To jest cały powód, dla którego ta funkcja
 * nie jest pętlą po dwóch znakach.
 */
size_t JsonView::skipValue(const char* p, size_t n, size_t i) {
    i = skipWs(p, n, i);
    if (i >= n) return n;

    const char c = p[i];

    if (c == '"') {
        ++i;
        while (i < n) {
            if (p[i] == '\\') { i += 2; continue; }
            if (p[i] == '"')  return i + 1;
            ++i;
        }
        return n;
    }

    if (c == '{' || c == '[') {
        const char open  = c;
        const char close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (i < n) {
            const char d = p[i];
            if (d == '"') {                      // napis w środku — pomiń w całości
                ++i;
                while (i < n) {
                    if (p[i] == '\\') { i += 2; continue; }
                    if (p[i] == '"') { ++i; break; }
                    ++i;
                }
                continue;
            }
            if (d == open)  ++depth;
            if (d == close) { --depth; if (depth == 0) return i + 1; }
            ++i;
        }
        return n;
    }

    // Liczba, true, false, null — kończy się na pierwszym znaku strukturalnym.
    while (i < n && p[i] != ',' && p[i] != '}' && p[i] != ']' &&
           p[i] != ' ' && p[i] != '\t' && p[i] != '\n' && p[i] != '\r') {
        ++i;
    }
    return i;
}

JsonType JsonView::type() const {
    if (!valid()) return JsonType::Invalid;
    const size_t i = skipWs(p_, n_, 0);
    if (i >= n_) return JsonType::Invalid;
    switch (p_[i]) {
        case '{': return JsonType::Object;
        case '[': return JsonType::Array;
        case '"': return JsonType::String;
        case 't': case 'f': return JsonType::Bool;
        case 'n': return JsonType::Null;
        default:  return (p_[i] == '-' || (p_[i] >= '0' && p_[i] <= '9'))
                             ? JsonType::Number : JsonType::Invalid;
    }
}

JsonView JsonView::get(const char* key) const {
    if (type() != JsonType::Object || key == nullptr) return {};

    const size_t keyLen = strlen(key);
    size_t i = skipWs(p_, n_, 0) + 1;   // za '{'

    while (i < n_) {
        i = skipWs(p_, n_, i);
        if (i >= n_ || p_[i] == '}') break;
        if (p_[i] != '"') break;        // dokument niepoprawny — kończymy

        const size_t nameStart = i + 1;
        const size_t nameEnd   = skipValue(p_, n_, i) - 1;   // bez cudzysłowu
        i = nameEnd + 1;

        i = skipWs(p_, n_, i);
        if (i >= n_ || p_[i] != ':') break;
        ++i;

        const size_t valueStart = skipWs(p_, n_, i);
        const size_t valueEnd   = skipValue(p_, n_, valueStart);

        // Porównanie po surowej treści klucza. Klucze protokołu to ASCII bez
        // ucieczek, więc rozwijanie ich tutaj byłoby pracą dla nikogo.
        if (nameEnd >= nameStart && (nameEnd - nameStart) == keyLen &&
            memcmp(p_ + nameStart, key, keyLen) == 0) {
            return JsonView(p_ + valueStart, valueEnd - valueStart);
        }

        i = skipWs(p_, n_, valueEnd);
        if (i < n_ && p_[i] == ',') ++i;
    }
    return {};
}

JsonView JsonView::at(size_t index) const {
    if (type() != JsonType::Array) return {};

    size_t i = skipWs(p_, n_, 0) + 1;   // za '['
    size_t seen = 0;

    while (i < n_) {
        i = skipWs(p_, n_, i);
        if (i >= n_ || p_[i] == ']') break;

        const size_t start = i;
        const size_t end   = skipValue(p_, n_, i);
        if (seen == index) return JsonView(p_ + start, end - start);
        ++seen;

        i = skipWs(p_, n_, end);
        if (i < n_ && p_[i] == ',') ++i;
    }
    return {};
}

size_t JsonView::size() const {
    const JsonType t = type();
    if (t != JsonType::Array && t != JsonType::Object) return 0;

    const bool object = (t == JsonType::Object);
    size_t i = skipWs(p_, n_, 0) + 1;
    size_t count = 0;

    while (i < n_) {
        i = skipWs(p_, n_, i);
        if (i >= n_ || p_[i] == (object ? '}' : ']')) break;

        if (object) {
            i = skipValue(p_, n_, i);          // klucz
            i = skipWs(p_, n_, i);
            if (i < n_ && p_[i] == ':') ++i;
        }
        i = skipValue(p_, n_, i);
        ++count;

        i = skipWs(p_, n_, i);
        if (i < n_ && p_[i] == ',') ++i;
    }
    return count;
}

bool JsonView::asString(char* out, size_t capacity) const {
    if (out == nullptr || capacity == 0) return false;
    out[0] = 0;
    if (type() != JsonType::String) return false;

    const size_t start = skipWs(p_, n_, 0) + 1;
    size_t w = 0;

    for (size_t i = start; i < n_; ++i) {
        const char c = p_[i];
        if (c == '"') { out[w] = 0; return true; }

        char decoded = c;
        if (c == '\\' && i + 1 < n_) {
            ++i;
            switch (p_[i]) {
                case 'n': decoded = '\n'; break;
                case 'r': decoded = '\r'; break;
                case 't': decoded = '\t'; break;
                case 'b': decoded = '\b'; break;
                case 'f': decoded = '\f'; break;
                case 'u': {
                    // Tylko zakres ASCII. Wyższe punkty kodowe wymagałyby
                    // składania par zastępczych i kodowania UTF-8, a protokół
                    // MyCastle wysyła surowe UTF-8, nie \u — ta gałąź jest
                    // zabezpieczeniem, nie ścieżką roboczą.
                    if (i + 4 < n_) {
                        unsigned code = 0;
                        for (int k = 1; k <= 4; ++k) {
                            const char h = p_[i + static_cast<size_t>(k)];
                            code <<= 4;
                            if (h >= '0' && h <= '9')      code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        }
                        i += 4;
                        decoded = (code < 0x80) ? static_cast<char>(code) : '?';
                    }
                    break;
                }
                default: decoded = p_[i];
            }
        }

        if (w + 1 >= capacity) { out[w] = 0; return false; }
        out[w++] = decoded;
    }
    out[w] = 0;
    return false;
}

const char* JsonView::asStringOr(char* out, size_t capacity, const char* fallback) const {
    return asString(out, capacity) ? out : fallback;
}

bool JsonView::asBool(bool& out) const {
    if (type() != JsonType::Bool) return false;
    out = (p_[skipWs(p_, n_, 0)] == 't');
    return true;
}

bool JsonView::asInt(i32& out) const {
    if (type() != JsonType::Number) return false;
    // strtol potrzebuje zera kończącego, a widok go nie ma — kopiujemy tyle,
    // ile zajmuje najdłuższa sensowna liczba.
    char tmp[24];
    const size_t i = skipWs(p_, n_, 0);
    const size_t n = (n_ - i) < sizeof(tmp) - 1 ? (n_ - i) : sizeof(tmp) - 1;
    memcpy(tmp, p_ + i, n);
    tmp[n] = 0;

    char* end = nullptr;
    const long v = strtol(tmp, &end, 10);
    if (end == tmp) return false;
    out = static_cast<i32>(v);
    return true;
}

bool JsonView::asFloat(float& out) const {
    if (type() != JsonType::Number) return false;
    char tmp[32];
    const size_t i = skipWs(p_, n_, 0);
    const size_t n = (n_ - i) < sizeof(tmp) - 1 ? (n_ - i) : sizeof(tmp) - 1;
    memcpy(tmp, p_ + i, n);
    tmp[n] = 0;

    char* end = nullptr;
    const double v = strtod(tmp, &end);
    if (end == tmp) return false;
    out = static_cast<float>(v);
    return true;
}

}  // namespace json
}  // namespace hydra
