/** Testy arytmetyki stałoprzecinkowej Q16.16 (rozdz. 9, 15). */

#include "hydra_test.hpp"

#include "hydra/core/Fixed.hpp"

using namespace hydra;

TEST("Fixed: konwersje i zaokrąglanie") {
    CHECK_EQ(Fixed(3).toInt(), 3);
    CHECK_EQ(Fixed(-3).toInt(), -3);
    CHECK(Fixed(1.5f).toFloat() > 1.49f && Fixed(1.5f).toFloat() < 1.51f);

    // toInt() ucina w dół, round() zaokrągla do najbliższej
    CHECK_EQ(Fixed(2.9f).toInt(), 2);
    CHECK_EQ(Fixed(2.9f).round(), 3);
    CHECK_EQ(Fixed(2.4f).round(), 2);

    CHECK_EQ(Fixed(1).raw(), Fixed::kOne);
}

TEST("Fixed: dodawanie i odejmowanie") {
    CHECK(Fixed(2) + Fixed(3) == Fixed(5));
    CHECK(Fixed(2) - Fixed(5) == Fixed(-3));

    Fixed a(1.25f);
    a += Fixed(0.75f);
    CHECK(a == Fixed(2.0f));
}

TEST("Fixed: mnożenie i dzielenie zachowują część ułamkową") {
    const Fixed r = Fixed(1.5f) * Fixed(2.0f);
    CHECK(r == Fixed(3.0f));

    const Fixed q = Fixed(7.0f) / Fixed(2.0f);
    CHECK(q == Fixed(3.5f));

    // 0.1 * 10 nie jest dokładne w Q16.16 — sprawdzamy tolerancję 1/1000
    const Fixed t = Fixed(0.1f) * Fixed(10);
    CHECK(abs(t - Fixed(1.0f)) < Fixed(0.001f));
}

TEST("Fixed: dzielenie przez zero nasyca zamiast wysypywać program") {
    CHECK(Fixed(5) / Fixed(0) == Fixed::fromRaw(INT32_MAX));
    CHECK(Fixed(-5) / Fixed(0) == Fixed::fromRaw(INT32_MIN));
}

TEST("Fixed: porównania i clamp") {
    CHECK(Fixed(1) < Fixed(2));
    CHECK(Fixed(2) >= Fixed(2));
    CHECK(clamp(Fixed(5), Fixed(0), Fixed(3)) == Fixed(3));
    CHECK(clamp(Fixed(-5), Fixed(0), Fixed(3)) == Fixed(0));
    CHECK(clamp(Fixed(2), Fixed(0), Fixed(3)) == Fixed(2));
    CHECK(min(Fixed(1), Fixed(2)) == Fixed(1));
    CHECK(max(Fixed(1), Fixed(2)) == Fixed(2));
}

TEST("real_t: ten sam kod działa na float i na Q16.16") {
    // Regulator napisany raz — typ liczbowy wybiera platforma (rozdz. 9).
    const real_t kp    = real(2.0f);
    const real_t error = real(1.5f);
    const real_t out   = kp * error;
    CHECK(toFloat(out) > 2.99f && toFloat(out) < 3.01f);

    CHECK(toFloat(clamp(real(10.0f), real(0.0f), real(5.0f))) == 5.0f);
}
