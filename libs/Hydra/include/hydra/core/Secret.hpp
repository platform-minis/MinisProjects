#pragma once
/**
 * Hydra — wartość, która nie ma prawa trafić do logu (rozdz. 7.3).
 *
 * Poświadczenia nigdy nie trafiają do logów i framework to wymusza typem,
 * a nie dyscypliną programisty: Secret<T> nie ma niejawnej konwersji do typu
 * bazowego, a jego reprezentacja tekstowa jest zawsze maską. Żeby dostać się
 * do zawartości, trzeba jawnie wywołać reveal() — wywołanie widoczne w kodzie
 * i łatwe do znalezienia w przeglądzie.
 *
 *     Secret<char[64]> psk;
 *     HYDRA_LOGI("hasło: %s", psk.masked());   // "••••••••"
 *     wifi.connect(ssid, psk.reveal());        // jawne, świadome odsłonięcie
 */

#include <string.h>

#include "hydra/core/Types.hpp"

namespace hydra {

/** Maska pokazywana zamiast zawartości. Stała długość nie zdradza długości sekretu. */
constexpr const char* kSecretMask = "********";

/**
 * Napis poufny o stałej pojemności. Nie alokuje i nie zostawia kopii —
 * clear() nadpisuje bufor zerami.
 */
template <size_t N>
class SecretString {
public:
    SecretString() = default;
    explicit SecretString(const char* value) { set(value); }
    ~SecretString() { clear(); }

    SecretString(const SecretString& o) { set(o.data_); }
    SecretString& operator=(const SecretString& o) {
        if (this != &o) set(o.data_);
        return *this;
    }

    Status set(const char* value) {
        clear();
        if (!value) return ok();
        const size_t len = strlen(value);
        if (len >= N) return fail(Err::OutOfRange);
        memcpy(data_, value, len + 1);
        len_ = len;
        return ok();
    }

    /** Jawne odsłonięcie zawartości — jedyna droga do wartości. */
    const char* reveal() const { return data_; }

    /** Reprezentacja bezpieczna do logowania. */
    const char* masked() const { return len_ == 0 ? "(puste)" : kSecretMask; }

    size_t length() const { return len_; }
    bool   empty() const { return len_ == 0; }

    void clear() {
        // Nadpisanie, a nie samo wyzerowanie długości: bufor bywa potem
        // zwalniany albo zrzucany razem z pamięcią po awarii.
        memset(data_, 0, N);
        len_ = 0;
    }

    bool operator==(const SecretString& o) const {
        return len_ == o.len_ && memcmp(data_, o.data_, len_) == 0;
    }

private:
    char   data_[N] = {};
    size_t len_     = 0;
};

}  // namespace hydra
