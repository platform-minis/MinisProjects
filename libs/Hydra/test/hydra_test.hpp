#pragma once
/**
 * Hydra — minimalny harness testów jednostkowych dla buildu hostowego.
 *
 * Świadomie bez zewnętrznych zależności (doctest/Unity): testy rdzenia muszą
 * dać się uruchomić na czystym systemie z samym kompilatorem, także w CI bez
 * dostępu do sieci. Rejestracja przypadków odbywa się przez konstruktory
 * obiektów statycznych, więc dodanie pliku test_*.cpp wystarcza.
 */

#include <stdio.h>
#include <string.h>

#include <type_traits>

namespace hydratest {

struct Case {
    const char* name;
    void (*fn)();
    Case* next;
};

void registerCase(Case* c);
int  runAll(const char* filter);
void reportFailure(const char* expr, const char* file, int line, const char* detail);
void countCheck();

struct Registrar {
    explicit Registrar(Case* c) { registerCase(c); }
};

/** Formatuje wartość do komunikatu o błędzie — tylko dla typów, które umiemy. */
template <typename T>
void describe(char* out, size_t cap, const T& v) {
    if constexpr (std::is_same<T, bool>::value) {
        snprintf(out, cap, "%s", v ? "true" : "false");
    } else if constexpr (std::is_integral<T>::value) {
        snprintf(out, cap, "%lld", static_cast<long long>(v));
    } else if constexpr (std::is_enum<T>::value) {
        snprintf(out, cap, "%lld", static_cast<long long>(static_cast<int>(v)));
    } else if constexpr (std::is_floating_point<T>::value) {
        snprintf(out, cap, "%f", static_cast<double>(v));
    } else if constexpr (std::is_pointer<T>::value) {
        snprintf(out, cap, "%p", static_cast<const void*>(v));
    } else {
        snprintf(out, cap, "<?>");
    }
}

inline bool check(bool cond, const char* expr, const char* file, int line) {
    countCheck();
    if (!cond) reportFailure(expr, file, line, nullptr);
    return cond;
}

template <typename A, typename B>
bool checkEq(const A& a, const B& b, const char* expr, const char* file, int line) {
    countCheck();
    if (a == b) return true;
    char sa[64], sb[64], detail[160];
    describe(sa, sizeof(sa), a);
    describe(sb, sizeof(sb), b);
    snprintf(detail, sizeof(detail), "lewa = %s, prawa = %s", sa, sb);
    reportFailure(expr, file, line, detail);
    return false;
}

inline bool checkStrEq(const char* a, const char* b, const char* expr, const char* file,
                       int line) {
    countCheck();
    if (a && b && strcmp(a, b) == 0) return true;
    char detail[256];
    snprintf(detail, sizeof(detail), "lewa = \"%s\", prawa = \"%s\"", a ? a : "(null)",
             b ? b : "(null)");
    reportFailure(expr, file, line, detail);
    return false;
}

}  // namespace hydratest

#define HYDRA_TEST_CAT2(a, b) a##b
#define HYDRA_TEST_CAT(a, b) HYDRA_TEST_CAT2(a, b)

/** Definiuje przypadek testowy: TEST("nazwa") { ... } */
#define TEST(nameLiteral)                                                              \
    static void HYDRA_TEST_CAT(hydra_test_fn_, __LINE__)();                            \
    static ::hydratest::Case HYDRA_TEST_CAT(hydra_test_case_, __LINE__){               \
        nameLiteral, HYDRA_TEST_CAT(hydra_test_fn_, __LINE__), nullptr};               \
    static ::hydratest::Registrar HYDRA_TEST_CAT(hydra_test_reg_, __LINE__){           \
        &HYDRA_TEST_CAT(hydra_test_case_, __LINE__)};                                  \
    static void HYDRA_TEST_CAT(hydra_test_fn_, __LINE__)()

// Makra są wariadyczne, bo preprocesor nie odróżnia przecinka w argumentach
// szablonu od separatora argumentów makra: CHECK(f<A, B>()) bez tego nie
// kompiluje się z komunikatem, który nijak nie wskazuje przyczyny.
#define CHECK(...)       ::hydratest::check((__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)
#define CHECK_EQ(a, b)   ::hydratest::checkEq((a), (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_STR(a, b)  ::hydratest::checkStrEq((a), (b), #a " == " #b, __FILE__, __LINE__)
#define REQUIRE(...)                                                                   \
    do {                                                                               \
        if (!::hydratest::check((__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__))       \
            return;                                                                    \
    } while (0)
