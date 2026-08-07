/** Testy propagacji błędów bez wyjątków (rozdz. 11). */

#include "hydra_test.hpp"

#include "hydra/core/Delegate.hpp"
#include "hydra/core/Expected.hpp"

using namespace hydra;

namespace {

Result<int> divide(int a, int b) {
    if (b == 0) return unexpected(Err::BadArgument);
    return a / b;
}

Status writeReg(bool fail) {
    if (fail) return ::hydra::fail(Err::IoError);
    return ok();
}

/** Funkcja korzystająca z propagacji w stylu operatora `?`. */
Result<int> chained(int b) {
    HYDRA_TRY(const int v, divide(100, b));
    HYDRA_CHECK(writeReg(false));
    return v * 2;
}

/**
 * Kilka propagacji w jednym zakresie — typowy kształt sterownika czytającego
 * po kolei kilka rejestrów. Wymaga, by makro tworzyło unikalne nazwy zmiennych
 * pomocniczych; bez tego druga instrukcja kolidowałaby z pierwszą.
 */
Result<int> threeInARow(int b) {
    HYDRA_TRY(const int first, divide(60, b));
    HYDRA_TRY(const int second, divide(90, b));
    HYDRA_TRY(const int third, divide(150, b));
    return first + second + third;
}

}  // namespace

TEST("expected: wartość i błąd") {
    auto good = divide(10, 2);
    CHECK(good.has_value());
    CHECK(static_cast<bool>(good));
    CHECK_EQ(*good, 5);
    CHECK(good.error() == Err::None);

    auto bad = divide(10, 0);
    CHECK(!bad.has_value());
    CHECK(bad.error() == Err::BadArgument);
    CHECK_EQ(bad.value_or(-1), -1);
}

TEST("expected<void>: status operacji bez wyniku") {
    CHECK(writeReg(false).has_value());
    CHECK(!writeReg(true).has_value());
    CHECK(writeReg(true).error() == Err::IoError);
}

TEST("HYDRA_TRY propaguje błąd w górę bez wyjątków") {
    auto okRes = chained(4);
    CHECK(okRes.has_value());
    CHECK_EQ(*okRes, 50);

    auto errRes = chained(0);
    CHECK(!errRes.has_value());
    CHECK(errRes.error() == Err::BadArgument);
}

TEST("HYDRA_TRY: kilka propagacji w jednym zakresie") {
    auto sum = threeInARow(3);
    REQUIRE(sum.has_value());
    CHECK_EQ(*sum, 20 + 30 + 50);

    auto err = threeInARow(0);
    CHECK(!err.has_value());
    CHECK(err.error() == Err::BadArgument);
}

TEST("expected: kopiowanie typu nietrywialnego woła destruktor raz") {
    static int alive = 0;
    struct Tracked {
        Tracked() { ++alive; }
        Tracked(const Tracked&) { ++alive; }
        ~Tracked() { --alive; }
    };
    alive = 0;
    {
        Result<Tracked> a{Tracked{}};
        CHECK(a.has_value());
        Result<Tracked> b = a;
        CHECK(b.has_value());
        CHECK_EQ(alive, 2);
    }
    CHECK_EQ(alive, 0);
}

TEST("toString(Err) pokrywa wszystkie kody") {
    CHECK_STR(toString(Err::None), "none");
    CHECK_STR(toString(Err::Timeout), "timeout");
    CHECK_STR(toString(Err::NotInitialized), "not-initialized");
}

TEST("HYDRA_TRY: działa wewnątrz argumentu innego makra") {
    // GCC przy rozwinięciu w argumencie makra nadaje wszystkim wystąpieniom
    // numer linii domykającej całe wywołanie — z __LINE__ trzy użycia poniżej
    // dostałyby tę samą nazwę zmiennej. Przypadek wzięty wprost ze sterownika
    // INA219, gdzie ujawnił się dopiero przy budowie wsadu.
    auto reader = [](int n) -> Result<int> { return n * 2; };

    auto outer = [&]() -> Status {
        HYDRA_CHECK([&]() -> Status {
            HYDRA_TRY(const int a, reader(1));
            HYDRA_TRY(const int b, reader(2));
            HYDRA_TRY(const int c, reader(3));
            if (a + b + c != 12) return fail(Err::Internal);
            return ok();
        }());
        return ok();
    };
    CHECK(outer().has_value());
}
