#pragma once
/**
 * Hydra — model wiadomości platformy MyCastle (moduł `minis`).
 *
 * Cały protokół MyCastle sprowadza się do jednej rzeczy: **zaadresowanej
 * wiadomości z ładunkiem JSON**. Adresem jest para (użytkownik, urządzenie),
 * rodzajem — jedna z kilkunastu nazw, ładunkiem — obiekt JSON. Na MQTT wygląda
 * to jak temat `minis/{user}/{device}/{rodzaj}`, ale temat jest *kodowaniem*
 * adresu, a nie adresem.
 *
 * To rozróżnienie jest tu wszystkim. Gdyby moduł operował na tematach MQTT,
 * przeniesienie go na RS-485 oznaczałoby wysyłanie napisu
 * „minis/user1/dev-iot3/telemetry" przy każdym pomiarze — 31 bajtów nagłówka
 * na łączu 9600 bodów, przy ładunku rzędu 40 bajtów. A przede wszystkim:
 * urządzenie pośrednie musiałoby parsować tematy, żeby cokolwiek przekazać
 * dalej.
 *
 * Dlatego `Frame` nie zna słowa „temat". Zna adres, rodzaj i ładunek —
 * a każde łącze koduje to po swojemu:
 *
 *   MqttLink   → minis/{user}/{device}/telemetry  + JSON
 *   SerialLink → [nagłówek 8 B z adresem węzła]   + JSON
 *
 * Dzięki temu bramka RS-485↔Ethernet przekłada ramkę z jednego kodowania na
 * drugie, nie zaglądając do ładunku ani razu.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_MINIS

#include "hydra/core/EventBus.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {
namespace minis {

/** Nazwa użytkownika w MyCastle. */
constexpr size_t kUserMax = 32;
/** Identyfikator urządzenia — u MinisLib jest nim numer seryjny. */
constexpr size_t kDeviceMax = 32;
/** Typ rozszerzenia: „vkbd", „vmouse", „display", „vfs". */
constexpr size_t kExtTypeMax = 16;
/** Pełny temat MQTT: minis/ + user + / + device + / + ext/{typ}/req. */
constexpr size_t kTopicMax = 8 + kUserMax + kDeviceMax + kExtTypeMax + 16;

/** Numer łącza w routerze. */
using LinkId = u8;
constexpr LinkId kNoLink = 0xFF;

/**
 * Adres urządzenia.
 *
 * Para, a nie jeden napis, bo obie części pochodzą z różnych miejsc:
 * użytkownik jest konfiguracją instalacji, urządzenie — tożsamością sztuki
 * sprzętu. Sklejanie ich w „user1/dev-iot3" oznaczałoby rozklejanie przy
 * każdym porównaniu.
 */
struct DeviceAddr {
    char user[kUserMax]     = {};
    char device[kDeviceMax] = {};

    bool valid() const { return user[0] != '\0' && device[0] != '\0'; }
    bool equals(const DeviceAddr& other) const;

    /** Ustawia obie części; dłuższe napisy są przycinane, nie odrzucane. */
    void set(const char* userName, const char* deviceName);

    static DeviceAddr of(const char* userName, const char* deviceName) {
        DeviceAddr a;
        a.set(userName, deviceName);
        return a;
    }
};

/**
 * Rodzaj wiadomości.
 *
 * Zamknięte wyliczenie, a nie napis, bo na łączu szeregowym rodzaj zajmuje
 * jeden bajt zamiast kilkunastu, a w kodzie `switch` wyłapuje przypadek,
 * o którym ktoś zapomniał. Rozszerzenia mają typ w osobnym polu — ich zbiór
 * jest otwarty i rośnie po stronie serwera.
 */
enum class MsgKind : u8 {
    Unknown = 0,
    // urządzenie → serwer
    Telemetry,
    Hello,
    Heartbeat,
    RegisterRequest,
    CommandAck,
    TwinReported,
    ExtResponse,
    // serwer → urządzenie
    Command,
    TwinDesired,
    ExtRequest,
};

/**
 * Czy wiadomość płynie do serwera. Decyduje o kierunku trasowania.
 *
 * Nazwa celowo różni się od `ILink::isUplink()`: tam chodzi o kierunek
 * **łącza**, tutaj o kierunek **wiadomości**. Jedna nazwa dla obu rzeczy
 * kończyła się przesłonięciem funkcji wolnej przez metodę i błędem kompilacji
 * w miejscu, które z tym rozróżnieniem nie miało nic wspólnego.
 */
constexpr bool flowsUpstream(MsgKind kind) {
    switch (kind) {
        case MsgKind::Telemetry:
        case MsgKind::Hello:
        case MsgKind::Heartbeat:
        case MsgKind::RegisterRequest:
        case MsgKind::CommandAck:
        case MsgKind::TwinReported:
        case MsgKind::ExtResponse:
            return true;
        default:
            return false;
    }
}

constexpr const char* toString(MsgKind kind) {
    switch (kind) {
        case MsgKind::Telemetry:       return "telemetry";
        case MsgKind::Hello:           return "hello";
        case MsgKind::Heartbeat:       return "heartbeat";
        case MsgKind::RegisterRequest: return "register-request";
        case MsgKind::Command:         return "command";
        case MsgKind::CommandAck:      return "command/ack";
        case MsgKind::TwinReported:    return "twin/reported";
        case MsgKind::TwinDesired:     return "twin/desired";
        case MsgKind::ExtRequest:      return "ext/req";
        case MsgKind::ExtResponse:     return "ext/res";
        case MsgKind::Unknown:         return "unknown";
    }
    return "unknown";
}

/**
 * Jedna wiadomość w drodze.
 *
 * Ładunek jest **widokiem**, nie kopią: ramka żyje przez czas jednego
 * przekazania, a bufor należy do łącza, które ją odebrało. Kopiowanie
 * kilkuset bajtów JSON-a przy każdym przeskoku przez bramkę byłoby jedyną
 * rzeczą, którą bramka naprawdę robi — i najdroższą.
 */
struct Frame {
    DeviceAddr addr;
    MsgKind    kind = MsgKind::Unknown;
    /** Wypełniony wyłącznie dla ExtRequest i ExtResponse. */
    char       extType[kExtTypeMax] = {};
    /** Ładunek JSON, bez zera kończącego. */
    CByteSpan  payload;

    /**
     * Ile razy ramka była już przekazywana.
     *
     * Bez tego licznika dwie bramki widzące się nawzajem odbijają ramkę
     * w nieskończoność i kładą oba łącza. Limit jest niski, bo instalacja
     * z czterema przeskokami to instalacja źle zaprojektowana, a nie taka,
     * którą trzeba obsłużyć.
     */
    u8 hops = 0;
    u8 qos  = 1;

    /**
     * Łącze, z którego ramka przyszła.
     *
     * Router nigdy nie odsyła ramki tam, skąd ją dostał — to najprostsza
     * i najskuteczniejsza ochrona przed pętlą, tańsza niż pamiętanie
     * identyfikatorów wiadomości.
     */
    LinkId ingress = kNoLink;
};

// ---------------------------------------------------------------------------
// Tematy MQTT — kodowanie adresu, używane przez MqttLink i przez testy
// ---------------------------------------------------------------------------

/**
 * Buduje temat `minis/{user}/{device}/{suffix}`.
 * Zwraca `false`, gdy nie mieści się w buforze — temat obcięty trafiłby do
 * innego urządzenia, a to gorsze niż niewysłanie wiadomości.
 */
bool buildTopic(char* out, size_t capacity, const DeviceAddr& addr,
                MsgKind kind, const char* extType = nullptr);

/**
 * Rozkłada temat na adres, rodzaj i typ rozszerzenia.
 * Zwraca `false` dla tematów spoza przestrzeni `minis/`.
 */
bool parseTopic(const char* topic, DeviceAddr& addr, MsgKind& kind,
                char* extType, size_t extCapacity);

// ---------------------------------------------------------------------------
// Zdarzenia
// ---------------------------------------------------------------------------

/** Dlaczego ramka nie dotarła. */
enum class DropReason : u8 {
    NoRoute = 0,    ///< brak trasy do adresata i brak trasy domyślnej
    HopLimit,       ///< ramka krążyła między bramkami
    LinkDown,       ///< łącze docelowe nie działa
    TooLarge,       ///< ładunek większy niż MTU łącza
    BadFrame,       ///< błąd sumy kontrolnej albo ramkowania
    Busy,           ///< kolejka nadawcza pełna
};

constexpr const char* toString(DropReason r) {
    switch (r) {
        case DropReason::NoRoute:  return "no-route";
        case DropReason::HopLimit: return "hop-limit";
        case DropReason::LinkDown: return "link-down";
        case DropReason::TooLarge: return "too-large";
        case DropReason::BadFrame: return "bad-frame";
        case DropReason::Busy:     return "busy";
    }
    return "unknown";
}

/**
 * Zmiana widoczności platformy.
 *
 * `online` mówi o łączu do serwera, nie o samym łączu fizycznym: bramka
 * z działającym RS-485 i zerwanym Ethernetem jest offline, bo węzły za nią
 * nie mają jak nic wysłać.
 */
struct MinisState {
    bool   online;
    LinkId uplink;      ///< którym łączem idzie ruch do serwera
    u16    routes;      ///< ile adresów router aktualnie zna
    u16    linksUp;     ///< maska działających łączy
};

/** Ramka odrzucona. Publikowane, bo cisza po nieudanej wysyłce jest błędem. */
struct MinisDropped {
    DropReason reason;
    LinkId     link;
    MsgKind    kind;
    u32        total;   ///< łączna liczba odrzuceń od startu
};

/** Router nauczył się, gdzie mieszka urządzenie. */
struct MinisRouteLearned {
    LinkId link;
    u8     slot;
    u16    routes;
};

}  // namespace minis
}  // namespace hydra

HYDRA_DECLARE_EVENT(hydra::minis::MinisState,        "minis/state")
HYDRA_DECLARE_EVENT(hydra::minis::MinisDropped,      "minis/dropped")
HYDRA_DECLARE_EVENT(hydra::minis::MinisRouteLearned, "minis/route")

#endif  // HYDRA_ENABLE_MINIS
