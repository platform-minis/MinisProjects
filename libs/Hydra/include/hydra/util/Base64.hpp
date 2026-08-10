#pragma once
/**
 * Hydra — base64 (RFC 4648).
 *
 * Istnieje z jednego powodu: obraz skryptu podróżuje w polu JSON-a, a JSON nie
 * niesie bajtów. Moduł WebAssembly zaczyna się od `00 61 73 6D` — pierwsze
 * zero ucięłoby napis w każdej implementacji operującej na `strlen`, a bajty
 * powyżej 0x7F nie są poprawnym UTF-8. Kodowanie jest więc warunkiem, a nie
 * wygodą.
 *
 * Koszt: cztery znaki na trzy bajty, czyli o jedną trzecią więcej ruchu.
 * Alternatywą byłby drugi kanał binarny obok JSON-owego — czyli drugi protokół
 * do utrzymania po obu stronach.
 *
 * Dekoder odrzuca wszystko, czego nie zna: żadnych spacji, żadnych znaków
 * końca wiersza, żadnego „na oko dobrze". Obraz różniący się jednym bajtem
 * i tak nie przejdzie weryfikacji skrótu, ale lepiej dowiedzieć się o tym
 * przy dekodowaniu niż po skopiowaniu ćwierć megabajta.
 */

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace util {

/** Ile bajtów zajmie zakodowanie `bytes` bajtów, bez zera kończącego. */
constexpr size_t base64EncodedSize(size_t bytes) { return ((bytes + 2) / 3) * 4; }

/** Górne oszacowanie długości po zdekodowaniu `chars` znaków. */
constexpr size_t base64DecodedSize(size_t chars) { return (chars / 4) * 3; }

/**
 * Koduje `data` do `out` jako tekst zakończony zerem.
 * Zwraca liczbę zapisanych znaków bez terminatora.
 */
Result<size_t> base64Encode(CByteSpan data, char* out, size_t capacity);

/**
 * Dekoduje `chars` znaków z `text` do `out`.
 *
 * `Err::BadArgument` dla znaku spoza alfabetu, długości niepodzielnej przez
 * cztery albo dopełnienia w środku. `Err::OutOfRange`, gdy wynik nie mieści
 * się w `out`.
 */
Result<size_t> base64Decode(const char* text, size_t chars, ByteSpan out);

}  // namespace util
}  // namespace hydra
