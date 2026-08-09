#pragma once
/**
 * Hydra — ramkowanie ramek MyCastle na łączu bajtowym (RS-485 / RS-232).
 *
 * Łącze szeregowe nie ma pojęcia wiadomości: daje strumień bajtów, który
 * zaczyna się w środku, gubi znaki przy zakłóceniu i sklejał się z poprzednim
 * nadawcą na wspólnej magistrali. Trzy problemy, trzy odpowiedzi:
 *
 *  1. **Granica wiadomości** — COBS. Zerowy bajt nie występuje wewnątrz ramki
 *     po zakodowaniu, więc `0x00` jest jednoznacznym końcem. Alternatywa —
 *     ucieczki w stylu SLIP — w najgorszym przypadku podwaja długość, COBS
 *     dokłada jeden bajt na 254. Przy ładunku JSON, w którym zer nie ma prawie
 *     wcale, narzut wynosi dokładnie jeden bajt.
 *  2. **Resynchronizacja** — odbiornik, który zgubił się w połowie ramki,
 *     czeka na następne zero i zaczyna od nowa. Nie ma stanu, z którego nie
 *     da się wyjść; nie ma też timeoutu, który trzeba stroić.
 *  3. **Uszkodzenie** — CRC-16/CCITT. Na RS-485 przy 500 m i falowniku obok
 *     przekłamany bit jest normą, a nie awarią. Ramka bez sumy kontrolnej
 *     dotarłaby do parsera JSON i została odrzucona jako „niepoprawny
 *     dokument" — komunikat, który wskazuje na nadawcę zamiast na kabel.
 *
 * **Adresowanie.** Na magistrali nie ma miejsca na „minis/user1/dev-iot3"
 * w każdej ramce: to 22 bajty przy ładunku rzędu czterdziestu. Węzły mają
 * więc jednobajtowe adresy, a pełną tożsamość podają **raz**, w ramce
 * z ustawioną flagą `Ident`. Bramka zapamiętuje przypisanie i od tej chwili
 * tłumaczy w obie strony. Gdy usłyszy nieznany węzeł, odsyła `Discover`
 * i dostaje tożsamość bez udziału człowieka.
 *
 * Postać ramki przed zakodowaniem COBS:
 *
 *     0   wersja(4b) | flagi(4b)
 *     1   rodzaj wiadomości (MsgKind)
 *     2   węzeł nadawcy
 *     3   węzeł odbiorcy   (0 = bramka, 255 = rozgłoszenie)
 *     4   licznik przeskoków
 *     5-6 długość ładunku, młodszy bajt pierwszy
 *     [ident]  gdy flaga Ident: len(user) | user | len(device) | device
 *     [ext]    gdy flaga Ext:   len(typ)  | typ
 *     [...]    ładunek
 *     CRC-16 nad wszystkim powyżej, młodszy bajt pierwszy
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/Expected.hpp"
#include "hydra/minis/MinisTypes.hpp"

/**
 * Największy ładunek przechodzący magistralą.
 *
 * Dobrane pod `hello` z kilkoma encjami — to najdłuższa wiadomość, jaką węzeł
 * wysyła, i jedyna, której nie da się podzielić. Telemetria mieści się
 * w dwustu bajtach. Zwiększenie kosztuje dwa bufory tej wielkości na łącze.
 */
#ifndef HYDRA_MINIS_SERIAL_MTU
#  define HYDRA_MINIS_SERIAL_MTU 512
#endif

namespace hydra {
namespace minis {

/** Adres węzła na magistrali. */
using NodeId = u8;
/** Bramka — jedyny węzeł o ustalonym z góry adresie. */
constexpr NodeId kGatewayNode  = 0;
/** Rozgłoszenie do wszystkich węzłów. */
constexpr NodeId kBroadcastNode = 255;

/** Wersja formatu ramki. Zmiana oznacza niezgodność, nie ulepszenie. */
constexpr u8 kSerialVersion = 1;

/** Najkrótsza możliwa ramka: sam nagłówek i suma kontrolna. */
constexpr size_t kSerialHeaderSize = 7;
constexpr size_t kSerialCrcSize    = 2;

enum SerialFlags : u8 {
    /** Ramka niesie pełny adres MyCastle nadawcy. */
    kFlagIdent    = 0x1,
    /** Ramka niesie typ rozszerzenia. */
    kFlagExt      = 0x2,
    /** Prośba o przedstawienie się. Ładunek pusty. */
    kFlagDiscover = 0x4,
};

/** Ramka rozłożona na części — to, co odbiornik oddaje wyżej. */
struct SerialFrame {
    MsgKind    kind = MsgKind::Unknown;
    NodeId     src  = kBroadcastNode;
    NodeId     dst  = kGatewayNode;
    u8         hops = 0;
    u8         flags = 0;
    /** Wypełniony, gdy ustawiona flaga Ident. */
    DeviceAddr addr{};
    char       extType[kExtTypeMax] = {};
    CByteSpan  payload{};

    bool hasIdent() const { return (flags & kFlagIdent) != 0; }
    bool isDiscover() const { return (flags & kFlagDiscover) != 0; }
};

/** CRC-16/CCITT-FALSE: wielomian 0x1021, wartość początkowa 0xFFFF. */
u16 crc16(CByteSpan data, u16 seed = 0xFFFF);

/**
 * Składa ramkę i koduje ją w COBS, kończąc bajtem zerowym.
 *
 * Zwraca liczbę zapisanych bajtów albo `OutOfRange`, gdy bufor wyjściowy jest
 * za mały. Sprawdzenie jest jawne, bo alternatywą byłaby ramka obcięta —
 * poprawna dla COBS, odrzucona dopiero przez CRC u odbiorcy.
 */
Result<size_t> encodeSerial(const SerialFrame& frame, ByteSpan out);

/**
 * Rozkłada zawartość jednej ramki COBS (bez bajtu zerowego kończącego).
 *
 * Dekodowanie odbywa się **w miejscu**: bufor wejściowy jest nadpisywany
 * postacią rozkodowaną, a `payload` wskazuje w jego wnętrze. Dzięki temu
 * przekazanie ramki przez bramkę nie kopiuje ładunku ani razu.
 */
Status decodeSerial(ByteSpan raw, SerialFrame& out);

/** Ile bajtów najwyżej zajmie ramka o zadanym ładunku po zakodowaniu. */
constexpr size_t serialWorstCase(size_t payload) {
    const size_t body = kSerialHeaderSize + kSerialCrcSize + payload +
                        2 + kUserMax + kDeviceMax + 1 + kExtTypeMax;
    // COBS dokłada bajt na każde 254 i jeden na początek; plus zero kończące.
    return body + body / 254 + 2;
}

// ---------------------------------------------------------------------------
// COBS — wystawione osobno, bo mają własne testy
// ---------------------------------------------------------------------------

/** Koduje; nie dopisuje bajtu zerowego. Zwraca długość albo OutOfRange. */
Result<size_t> cobsEncode(CByteSpan in, ByteSpan out);
/** Dekoduje; wejście bez bajtu zerowego kończącego. */
Result<size_t> cobsDecode(CByteSpan in, ByteSpan out);

}  // namespace minis
}  // namespace hydra

#endif  // HYDRA_ENABLE_MINIS
