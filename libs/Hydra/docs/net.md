# Moduł `net` — łączność i protokoły

Łącze, gniazda i pięciu klientów protokołów: MQTT, WebSocket, TLS, HTTP i UDP.
Wszystko na jednym kontrakcie, dzięki czemu składa się w stos zamiast rosnąć
w szerz.

## Jedna decyzja, z której wynika reszta

Transport to **strumień bajtów**, opisany interfejsem `IClient`. Protokół nie
wie, co jest pod spodem, a transport nie wie, co przez niego płynie.

```cpp
class IClient {
    Status connect(const char* host, u16 port, u32 timeoutMs);
    void   stop();
    bool   connected() const;
    size_t write(CByteSpan data);
    size_t read(ByteSpan out);
    size_t available();
};
```

Cała siła jest w tym, że **`TlsClient` i `WebSocketClient` same są `IClient`**.
Nie są wariantami gniazda — są jego opakowaniami. Stąd stos składa się jak
klocki, bez ani jednej klasy pośredniej:

```
MqttClient  ──►  IClient
                   │
                   ├── gniazdo TCP                 mqtt://  (1883)
                   ├── TlsClient → gniazdo         mqtts:// (8883)
                   ├── WebSocketClient → gniazdo   ws://    (9001)
                   └── WebSocketClient → TlsClient → gniazdo   wss://
```

To samo dotyczy HTTP: `HttpClient` nad gniazdem to `http://`, nad `TlsClient`
to `https://` — i jest to **ta sama klasa z tym samym API**. Klient
z wbudowanym szyfrowaniem musiałby mieć drugi zestaw metod i drugi zestaw
kodów błędów.

Podział na `IClient` i `INetworkInterface` jest równie celowy: pierwsze to
gniazdo, drugie to łącze. `ConnectionManager` zajmuje się wyłącznie łączem,
protokoły wyłącznie gniazdem — dlatego zamiana Wi-Fi na Ethernet nie dotyka
ani jednej linijki w MQTT.

## Warstwy

```
aplikacja
   │
protokoły      MqttClient · HttpClient · UdpClient
   │
opakowania     WebSocketClient · TlsClient          ← same są IClient
   │
gniazda        IClient · IUdpSocket
   │
łącze          INetworkInterface (Wi-Fi, Ethernet)  ← ConnectionManager
   │
backend        src/net/arduino/   albo   src/net/mock/
```

## Łącze

`NetModule` prowadzi łącze i pilnuje go w tasku `net.worker`: ponawia próby
z rosnącym odstępem, przełącza na sieć zapasową, ogłasza stan przez `EventBus`.

```cpp
net::NetModule gNet(net::defaultNetworkInterface());

net::NetModule::Config cfg;
cfg.connection.backoffBaseMs = 1000;
cfg.mqtt.clientId  = "czujnik-1";
cfg.mqtt.host      = "broker.local";
cfg.mqtt.port      = 1883;
cfg.mdnsHostname   = "czujnik-1";
HYDRA_CHECK(gNet.configure(cfg));
```

Zdarzenia (`ConnStateChanged`, `NetGotAddress`, `NetLost`) idą magistralą,
więc reszta programu nie odpytuje o stan sieci — dowiaduje się o zmianie.

## MQTT

Pełny klient MQTT 3.1.1: QoS 0 i 1, testament (`will`), sesja trwała,
subskrypcje z symbolami wieloznacznymi.

```cpp
HYDRA_CHECK(gNet.mqtt().subscribe("dom/+/komenda", 1,
    [](const char* topic, CByteSpan payload) { /* … */ }));

HYDRA_CHECK(gNet.mqtt().publish("dom/salon/temperatura", "21.5"));
```

`loop(now)` woła task modułu; czas wchodzi argumentem, więc ponawianie
i wygasanie sesji da się przetestować w mikrosekundach zamiast czekać minuty.

Warstwę wyżej stoi [moduł `minis`](minis.md), który mówi już encjami MyCastle,
a nie tematami.

## WebSocket

Klient RFC 6455 — i jest to **`IClient` opakowujący `IClient`**. Powstał po to,
żeby MQTT chodził przez port 443 tam, gdzie zapora nie przepuszcza 1883, a nie
jako osobny protokół aplikacyjny.

```cpp
net::WebSocketClient ws(*iface.createClient());

net::WebSocketClient::Config wsCfg;
wsCfg.path        = "/mqtt";
wsCfg.subprotocol = "mqtt";   // bez tego broker zamyka po pierwszej ramce
ws.configure(wsCfg);

// Od tej chwili to zwykły strumień — MQTT nie wie o różnicy.
net::IClient& stream = ws;
```

Ramki binarne z maskowaniem (wymaganym od klienta), odpowiedź na `ping`,
sklejanie fragmentów w ciągły strumień.

## HTTP

Klient HTTP/1.1 nad dowolnym `IClient`.

```cpp
u8 scratch[512];
net::HttpClient http;
HYDRA_CHECK(http.begin(*iface.createClient(), scratch));

net::HttpClient::Request request;
request.method      = net::HttpClient::Method::Post;
request.host        = "api.example.com";
request.path        = "/v1/telemetry";
request.body        = CByteSpan{payload, payloadLength};
request.contentType = "application/json";

HYDRA_TRY(const auto response, http.send(request));
if (!response.ok()) HYDRA_LOGW("serwer odmówił: %u", response.status);
```

### Treść płynie strumieniem

Odpowiedź przychodzi kawałkami do wywołania zwrotnego, nie do bufora:

```cpp
auto response = http.get("http://example.com/firmware.bin",
    net::HttpClient::BodyFn{[](CByteSpan chunk) { flash.write(chunk); }});
```

Dzięki temu pobranie wsadu 2 MB działa przy 512 bajtach RAM-u. Wariant
„zwróć całość w tablicy" nie działałby na urządzeniu w ogóle, więc go nie ma —
jest za to `getToBuffer()`, które robi to jawnie i zgłasza przepełnienie
zamiast uciąć po cichu.

### Status błędu to nie błąd wywołania

`404` i `500` wracają jako **poprawny wynik** z polem `status`. Błąd
`expected` oznacza, że rozmowa się nie odbyła: brak łączności, przekroczony
czas, odpowiedź niezgodna z protokołem. Bez tego podziału wołający musiałby
odróżniać „serwer odmówił" od „nie było sieci" po treści komunikatu.

### Co jeszcze jest

`Transfer-Encoding: chunked` razem z nagłówkami końcowymi (ich pominięcie
zatruwa kolejne żądanie na tym samym połączeniu), przekierowania 30x z limitem,
`keep-alive` z ponownym użyciem strumienia, `parseUrl()` do rozbioru adresu.

### Czego nie ma i dlaczego

| Brak | Powód |
|---|---|
| Kompresja treści | Rozpakowanie gzip wymaga okna słownika 32 KB. Na układzie z 64 KB RAM to nie kompromis, tylko koniec. Wysyłamy `Accept-Encoding: identity`, żeby serwer nie pakował. |
| HTTP/2 | Wielostrumieniowość i HPACK; wobec HTTP/1.1 z keep-alive nie wnosi nic do rozmowy z API urządzenia. |
| Ciasteczka | Klient nie ma sesji ani miejsca na jej trzymanie. |

## UDP

Datagramy mają **osobny interfejs**, `IUdpSocket`, a nie `IClient`. Różnica nie
jest kosmetyczna: strumień gubi granice wiadomości, a datagram jest
niepodzielny. Kod pisany pod `read()` zwracające „tyle, ile jest" po
podłączeniu pod UDP cicho sklejałby albo dzielił pakiety.

```cpp
net::UdpClient udp;
HYDRA_CHECK(udp.begin(*iface.createUdp(), 5000));
HYDRA_CHECK(udp.setPeer(iface, "serwer.local", 5005));

HYDRA_CHECK(udp.send(CByteSpan{payload, sizeof(payload)}));

u8 buffer[512];
HYDRA_TRY(const auto reply, udp.receive(buffer, 1000));
```

### Filtr nadawcy jest domyślnie włączony

Na port, z którego wysłałeś zapytanie, może odpowiedzieć **ktokolwiek** —
wystarczy zgadnąć port i zdążyć przed prawdziwym serwerem. Na tym polega
zatruwanie odpowiedzi DNS i podszywanie się pod serwer czasu.

`UdpClient` odrzuca więc datagramy spoza ustawionego rozmówcy i liczy je
w `rejected()`. Filtr nie zastępuje uwierzytelnienia — adres da się sfałszować
— ale odsiewa przypadkowy ruch i wymusza świadomą decyzję tam, gdzie odpowiedzi
mają przychodzić od wielu stron:

```cpp
udp.acceptFromAnyone();   // wykrywanie urządzeń, nasłuch rozgłoszeń
```

Nazwa jest długa celowo: ma być widać w kodzie, że filtr zdjęto świadomie.

### Obcięcie datagramu jest zgłaszane

Gniazdo systemowe milczy, gdy pakiet nie zmieści się w buforze. My mówimy:

```cpp
if (reply.truncated) HYDRA_LOGW("pakiet nie zmieścił się — reszta przepadła");
```

Przy strumieniu resztę doczytałoby się później. Przy datagramie **nie ma czego
doczytywać**, a cicha utrata ogona daje błędy wyglądające na uszkodzenie danych
po drugiej stronie.

### Rozgłaszanie

Osobna metoda, bo trafia do każdego urządzenia w sieci i nie powinno się
wydarzyć przez pomyłkę w adresie. Zgodę na rozgłaszanie gniazdo dostaje dopiero
przy pierwszym użyciu:

```cpp
HYDRA_CHECK(udp.broadcast(5005, CByteSpan{hello, sizeof(hello)}));
```

## Testowanie bez sieci

`src/net/mock/` daje atrapy całej warstwy. Test wstrzykuje bajty, które
„przyszły z serwera", i ogląda to, co klient wysłał:

```cpp
mock::MockClient transport;
transport.injectRx(response);          // odpowiedź serwera
// … kod protokołu …
CHECK(transport.sent() /* zawiera oczekiwane żądanie */);
```

`MockUdp` zachowuje **granice datagramów** i odmawia rozgłoszenia bez włączonej
zgody — atrapa sklejająca pakiety w strumień przepuściłaby kod, który na
prawdziwej sieci gubi wiadomości. `MockNetwork::addHost()` buduje tablicę nazw;
nazwa nieznana daje `Err::NotFound`, a nie adres zastępczy, żeby literówka
wywalała się w teście, a nie na wysyłce pod 0.0.0.0.

Dzięki temu maszyna stanów połączenia, cały MQTT, parser HTTP i obsługa UDP
sprawdzają się na PC w milisekundach. Testy: `test/test_net.cpp`,
`test/test_websocket.cpp`, `test/test_http_udp.cpp`.

## Backendy

| Katalog | Kiedy |
|---|---|
| `src/net/arduino/` | ESP32, RP2 (`WiFiClient`, `WiFiUDP`), STM32 (`EthernetClient`, `EthernetUDP`) |
| `src/net/mock/` | build hostowy i testy |

Backend wybiera emiter CMake na podstawie celu; nagłówki Arduino nie mają prawa
pojawić się poza `src/*/arduino/` i pilnuje tego `tools/check_includes.sh`.

## Co wnosi każdy klient

Rzędy wielkości, nie obietnice — zależą od kompilatora i poziomu optymalizacji.

| Element | Flash | RAM |
|---|---|---|
| `IClient` + backend | ~1 KB | rozmiar gniazda platformy |
| `MqttClient` | ~6 KB | bufor pakietu + tablica subskrypcji |
| `WebSocketClient` | ~3 KB | bufor ramki |
| `HttpClient` | ~4 KB | bufor roboczy wołającego (≥256 B) |
| `UdpClient` | ~1 KB | bufor datagramu wołającego |

Każdy jest osobną jednostką translacji — czego nie zawołasz, tego konsolidator
nie wciągnie.

## Czego w module nie ma

- **Serwera.** Ani HTTP, ani TCP. Urządzenie zwykle łączy się na zewnątrz,
  a nasłuch to osobna decyzja projektowa z osobnymi skutkami dla bezpieczeństwa.
- **IPv6.** `Endpoint` niesie adres IPv4 w 32 bitach.
- **DNS-a własnego.** `resolve()` oddaje sprawę stosowi platformy.
- **Kolejkowania z zapisem na nośnik.** Wiadomość niewysłana ginie razem
  z zasilaniem; trwałość to zadanie warstwy wyżej.
