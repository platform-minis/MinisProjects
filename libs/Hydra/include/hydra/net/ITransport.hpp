#pragma once
/**
 * Hydra — abstrakcja transportu sieciowego (rozdz. 7).
 *
 * Specyfikacja opiera transport na Arduinowych interfejsach Client / UDP /
 * Server, które implementują wszystkie trzy ekosystemy (WiFiClient na ESP32
 * i Pico W, EthernetClient/W5500 na STM32). Hydra powtarza ten kształt we
 * własnych interfejsach, zamiast używać typów Arduino wprost — inaczej
 * warstwa protokołów przestałaby dać się skompilować i przetestować na PC,
 * a nagłówki Arduino wyciekłyby poza src/hal/arduino/.
 *
 * Podział jest celowy: IClient to strumień bajtów, INetworkInterface to
 * łącze. ConnectionManager zajmuje się wyłącznie tym drugim, MQTT wyłącznie
 * pierwszym — dzięki temu zmiana Wi-Fi na Ethernet nie dotyka protokołów.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_NET

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"
#include "hydra/net/NetTypes.hpp"

namespace hydra {
namespace net {

/** Gniazdo TCP widziane jako strumień bajtów. */
class IClient {
public:
    virtual ~IClient() = default;

    virtual Status connect(const char* host, u16 port, u32 timeoutMs = 5000) = 0;
    virtual void   stop()                                                    = 0;
    virtual bool   connected() const                                         = 0;

    /** Zwraca liczbę zapisanych bajtów; mniej niż podano oznacza kłopot. */
    virtual size_t write(CByteSpan data) = 0;
    /** Odczyt bez blokowania — tyle, ile jest gotowe. */
    virtual size_t read(ByteSpan out)    = 0;
    virtual size_t available()           = 0;

    /** Odczyt dokładnie n bajtów albo timeout. */
    Status readExactly(ByteSpan out, u32 timeoutMs);
};

/** Łącze sieciowe: Wi-Fi, Ethernet albo cokolwiek innego. */
class INetworkInterface {
public:
    virtual ~INetworkInterface() = default;

    /** Nazwa do logów: "wifi", "eth". */
    virtual const char* name() const = 0;

    /** Przygotowanie sprzętu. Wołane raz, przy inicjalizacji modułu. */
    virtual Status begin() = 0;

    /**
     * Rozpoczyna łączenie. Wywołanie nie blokuje do skutku — postęp
     * sprawdza ConnectionManager przez linkUp(), bo blokowanie na kilkanaście
     * sekund zatrzymałoby task sieciowy razem z całą obsługą protokołów.
     */
    virtual Status connect(const NetworkCredentials& creds) = 0;
    virtual void   disconnect() = 0;

    virtual bool linkUp() const = 0;
    /** Adres IPv4 w kolejności bajtów hosta; 0 = brak adresu. */
    virtual u32  localIpV4() const = 0;
    /** Siła sygnału w dBm; 0 dla łączy przewodowych. */
    virtual i8   rssiDbm() const { return 0; }

    /** Tworzy gniazdo klienckie. Zwrócony obiekt należy do interfejsu. */
    virtual IClient* createClient() = 0;
};

/** Ogłaszanie usługi w sieci lokalnej (rozdz. 7.2). */
class IMdns {
public:
    virtual ~IMdns() = default;
    /** Ogłasza urządzenie jako <hostname>.local. */
    virtual Status begin(const char* hostname) = 0;
    virtual Status addService(const char* service, const char* proto, u16 port) = 0;
    virtual void   end() = 0;
};

/**
 * Domyślny interfejs sieciowy platformy — Wi-Fi na ESP32 i RP2, Ethernet na
 * STM32. Definiuje go backend wchodzący do buildu, więc aplikacja nie musi
 * wiedzieć, który to.
 */
INetworkInterface& defaultNetworkInterface();
IMdns&             defaultMdns();

}  // namespace net
}  // namespace hydra

#endif  // HYDRA_ENABLE_NET
