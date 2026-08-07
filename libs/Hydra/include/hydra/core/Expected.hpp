#pragma once
/**
 * Hydra — propagacja błędów bez wyjątków (rozdz. 11).
 *
 * Wyjątki C++ i RTTI są w Hydrze wyłączone; każda operacja mogąca się nie udać
 * zwraca expected<T, Err>. Interfejs w duchu std::expected (C++23) / Rust Result.
 */

#include <new>
#include <type_traits>
#include <utility>

#include "hydra/core/Types.hpp"

namespace hydra {

/** Znacznik konstrukcji wariantu błędnego: return unexpected(Err::Timeout); */
template <typename E>
struct Unexpected {
    E error;
};

template <typename E>
constexpr Unexpected<E> unexpected(E e) { return Unexpected<E>{e}; }

template <typename T, typename E = Err>
class expected {
    static_assert(!std::is_reference<T>::value, "expected<T&> nie jest wspierane");

public:
    using value_type = T;
    using error_type = E;

    constexpr expected() : has_(true) { new (storage()) T(); }
    expected(const T& v) : has_(true) { new (storage()) T(v); }
    expected(T&& v) : has_(true) { new (storage()) T(std::move(v)); }
    expected(Unexpected<E> u) : has_(false), err_(u.error) {}

    expected(const expected& o) : has_(o.has_), err_(o.err_) {
        if (has_) new (storage()) T(*o.ptr());
    }
    expected(expected&& o) : has_(o.has_), err_(o.err_) {
        if (has_) new (storage()) T(std::move(*o.ptr()));
    }
    expected& operator=(const expected& o) {
        if (this == &o) return *this;
        reset();
        has_ = o.has_; err_ = o.err_;
        if (has_) new (storage()) T(*o.ptr());
        return *this;
    }
    expected& operator=(expected&& o) {
        if (this == &o) return *this;
        reset();
        has_ = o.has_; err_ = o.err_;
        if (has_) new (storage()) T(std::move(*o.ptr()));
        return *this;
    }
    ~expected() { reset(); }

    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }

    T&       value()       { return *ptr(); }
    const T& value() const { return *ptr(); }
    T&       operator*()       { return *ptr(); }
    const T& operator*() const { return *ptr(); }
    T*       operator->()       { return ptr(); }
    const T* operator->() const { return ptr(); }

    /** Wartość albo podany fallback — bez sprawdzania po stronie wołającego. */
    T value_or(T fallback) const { return has_ ? *ptr() : fallback; }

    /** Kod błędu. Dla wariantu z wartością zwraca E{} (czyli Err::None). */
    constexpr E error() const { return has_ ? E{} : err_; }

private:
    void reset() { if (has_) ptr()->~T(); has_ = false; }
    void*       storage()       { return static_cast<void*>(&buf_); }
    T*          ptr()           { return reinterpret_cast<T*>(&buf_); }
    const T*    ptr()     const { return reinterpret_cast<const T*>(&buf_); }

    typename std::aligned_storage<sizeof(T), alignof(T)>::type buf_;
    bool has_;
    E    err_ = E{};
};

/** Specjalizacja dla operacji bez wyniku — expected<void, Err>. */
template <typename E>
class expected<void, E> {
public:
    using value_type = void;
    using error_type = E;

    constexpr expected() : has_(true) {}
    constexpr expected(Unexpected<E> u) : has_(false), err_(u.error) {}

    constexpr bool has_value() const { return has_; }
    constexpr explicit operator bool() const { return has_; }
    constexpr E error() const { return has_ ? E{} : err_; }
    constexpr void value() const {}

private:
    bool has_;
    E    err_ = E{};
};

/** Skrót dla najczęstszego przypadku: wynik + kod błędu Hydry. */
template <typename T>
using Result = expected<T, Err>;
using Status = expected<void, Err>;

/** Sukces operacji bez wyniku. */
inline Status ok() { return Status{}; }
/** Porażka operacji bez wyniku. */
inline Status fail(Err e) { return Status{unexpected(e)}; }

/**
 * Sklejanie z rozwinięciem argumentów. Bez tej podwójnej warstwy `a##__COUNTER__`
 * dałoby dosłowne „_hydra_try___COUNTER__", więc drugie użycie HYDRA_TRY w tym
 * samym zakresie kolidowałoby z pierwszym.
 */
#define HYDRA_CONCAT_IMPL(a, b) a##b
#define HYDRA_CONCAT(a, b) HYDRA_CONCAT_IMPL(a, b)

/**
 * Źródło unikalnego przyrostka nazwy.
 *
 * __COUNTER__, a nie __LINE__: przy rozwinięciu wewnątrz argumentu innego makra
 * GCC przypisuje wszystkim wystąpieniom numer linii domykającej całe wywołanie.
 * Trzy HYDRA_TRY w domknięciu przekazanym do HYDRA_CHECK dostawały wtedy tę
 * samą nazwę i kompilacja padała na kolizji deklaracji. Clang numeruje inaczej,
 * więc na hoście problem nie występował — i nie występowałby aż do pierwszego
 * prawdziwego buildu.
 */
#if defined(__COUNTER__)
#  define HYDRA_UNIQUE_ID __COUNTER__
#else
#  define HYDRA_UNIQUE_ID __LINE__
#endif

/**
 * Propagacja błędu w stylu operatora `?` z Rusta.
 * Użycie:  HYDRA_TRY(const auto v, bus.read(reg));
 */
#define HYDRA_TRY(decl, expr) HYDRA_TRY_IMPL(decl, expr, HYDRA_CONCAT(_hydra_try_, HYDRA_UNIQUE_ID))

#define HYDRA_TRY_IMPL(decl, expr, tmp)                 \
    auto tmp = (expr);                                  \
    if (!tmp) return ::hydra::unexpected(tmp.error());  \
    decl = *tmp

/** Wersja dla wyrażeń bez wyniku. */
#define HYDRA_CHECK(expr)                                       \
    do {                                                        \
        auto _hydra_chk = (expr);                               \
        if (!_hydra_chk) return ::hydra::unexpected(_hydra_chk.error()); \
    } while (0)

}  // namespace hydra
