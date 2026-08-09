# Moduł `minis` — platforma IoT MyCastle

Encje, telemetria, komendy i rozszerzenia MyCastle, niezależnie od tego, czym
urządzenie jest podłączone. MQTT, MQTT po WebSockecie, RS-485, RS-232 —
i urządzenia pośrednie, które przekładają jedno na drugie.

## Jedna decyzja, z której wynika reszta

Protokół MyCastle to **zaadresowana wiadomość z ładunkiem JSON**. Na MQTT
wygląda jak temat `minis/{user}/{device}/telemetry`, ale temat jest
*kodowaniem* adresu, a nie adresem.

```
Frame { DeviceAddr{user, device}, MsgKind, extType, payload }
```

Gdyby moduł operował na tematach, przeniesienie go na RS-485 oznaczałoby
wysyłanie 31 bajtów napisu przy ładunku rzędu czterdziestu — a przede
wszystkim: bramka musiałaby parsować tematy, żeby cokolwiek przekazać dalej.

Stąd warstwy:

```
aplikacja      encje, sendTelemetry(), onCommand(), extRespond()
   │
MinisIotModule buduje i rozbiera JSON
   │
Router         dokąd to idzie; uczy się topologii z ruchu
   │
ILink          MqttLink  ·  SerialLink  ·  (własne)
   │
transport      IClient → WebSocketClient → TlsClient → gniazdo
```

## Węzeł

```cpp
Entity gTemp  = Entity::sensor("temperature", "Temperatura", "temperature", "°C");
Entity gRelay = Entity::toggle("relay", "Przekaźnik", [](json::JsonView v) {
    return v.asBool(gRelayOn);
});

MinisIotModule::Config cfg;
cfg.self = DeviceAddr::of("user1", "dev-iot3");
minis.configure(cfg);
minis.addLink(mqtt);
minis.addEntity(gTemp);
minis.addEntity(gRelay);
App::config().add(minis);
```

Encje idą w `hello`, więc panel MyCastle rysuje z nich interfejs sam. Encja
zapisywalna przechwytuje komendę o nazwie równej swojemu identyfikatorowi
i **sama ją potwierdza** — nie ma ręcznego `ackCommand()`.

Przykłady: [`examples/minis-node`](../examples/minis-node/),
[`examples/minis-gateway`](../examples/minis-gateway/).

## Bramka

Całe „bycie bramką" to dwa łącza w jednym routerze:

```cpp
minis.addLink(mqtt);    // do serwera
minis.addLink(rs485);   // do węzłów
```

Reszta dzieje się sama:

1. pierwsza ramka z węzła uczy router, że `dev-node3` stoi za magistralą,
2. router prosi łącze MQTT o nasłuch komend dla tego urządzenia,
3. komenda z panelu schodzi na magistralę pod właściwy numer węzła,
4. potwierdzenie wraca tą samą drogą.

**Nie ma tablicy urządzeń do utrzymania.** Dołożenie czwartego czujnika do
magistrali nie wymaga zmiany w bramce ani ponownego wgrania wsadu — i to jest
jedyny powód, dla którego uczenie tras w ogóle istnieje.

Uczymy się wyłącznie z ruchu **w górę** i wyłącznie z łączy **nieprowadzących
do serwera**. Z łącza do serwera przychodzi ruch wszystkich urządzeń świata;
nauka z niego dałaby trasę „każdy jest w internecie" i pętlę przy pierwszej
odpowiedzi.

Ochrona przed pętlą jest dwustopniowa i celowo prymitywna: ramka nigdy nie
wraca na łącze, z którego przyszła, a licznik przeskoków ubija ją, gdyby dwie
bramki widziały się nawzajem.

## Łącza

### MQTT

Cienka warstwa nad `net::MqttClient`. Subskrybuje **tylko** kierunek z serwera
(`command`, `twin/desired`, `ext/+/req`) — broker odsyła publikację także
nadawcy, więc zapisanie się na `#` zapętliłoby własną telemetrię.

Bramka robi to inaczej: symbol wieloznaczny na **miejscu urządzenia**
(`minis/user1/+/command`). Trzy subskrypcje zamiast trzech razy liczba węzłów;
przy domyślnym limicie ośmiu subskrypcji to różnica między działającą bramką
a taką, która obsługuje dwa pierwsze węzły i milczy o reszcie.

### WebSocket

`net::WebSocketClient` **jest** `IClient` i **opakowuje** `IClient`, więc nie
jest rodzeństwem MQTT, tylko warstwą pod nim:

```cpp
WiFiClientAdapter tcp;
WebSocketClient   ws{tcp};      // cfg.path = "/mqtt"
MqttClient        mqtt{ws};     // bez żadnej zmiany
```

Broker MyCastle (Aedes) słucha na `ws://{host}:1902/mqtt`, więc to główna
droga do platformy. `wss://` powstaje przez wsunięcie `TlsClient` pod spód,
a nie przez zmianę w kliencie WebSocket.

### RS-485 / RS-232

Ramkowanie: **COBS + CRC-16/CCITT**. COBS daje jednoznaczną granicę ramki
(zero) i resynchronizację bez timeoutu, przy narzucie jednego bajtu na 254 —
SLIP w najgorszym razie podwaja długość. CRC jest obowiązkowy: na 500 m obok
falownika przekłamany bit jest normą, a bez sumy kontrolnej uszkodzony ładunek
dochodzi do parsera JSON i zostaje zgłoszony jako „niepoprawny dokument",
czyli komunikat wskazujący na nadawcę zamiast na kabel.

**Adresowanie.** Na magistrali węzły mają numery jednobajtowe; pełną tożsamość
podają w pierwszych ramkach, a bramka zapamiętuje przypisanie. Nieznany węzeł
dostaje ramkę `Discover` i przedstawia się bez udziału człowieka. Jedyne, co
trzeba ustawić ręcznie przy montażu, to numer węzła — jak adres w Modbusie.

**Half-dupleks.** Pin DE nadajnika ustawia się przed wysłaniem i zwalnia
**po** opróżnieniu bufora sprzętowego (`flush()`). Zwolnienie za wcześnie
ucina koniec ramki, za późno — blokuje magistralę.

## Rozmiary statyczne

| Makro | Domyślnie | Co ogranicza |
|---|---|---|
| `HYDRA_MINIS_MAX_LINKS` | 4 | łącza w routerze |
| `HYDRA_MINIS_MAX_ROUTES` | 16 | urządzenia za bramką |
| `HYDRA_MINIS_MAX_NODES` | 16 | węzły znane łączu szeregowemu |
| `HYDRA_MINIS_MAX_ENTITIES` | 12 | encje urządzenia |
| `HYDRA_MINIS_MAX_EXTENSIONS` | 4 | rozszerzenia |
| `HYDRA_MINIS_TX_BUFFER` | 512 | największa składana wiadomość |
| `HYDRA_MINIS_SERIAL_MTU` | 512 | największy ładunek na magistrali |
| `HYDRA_WS_BUFFER` | 512 | bufor odbiorczy WebSocketu |

Przekroczenie każdego z nich jest **zgłaszane**, nie obcinane po cichu:
`hello`, które się nie zmieściło, oznaczałoby panel bez części elementów,
a obcięta telemetria — dokument odrzucony przez serwer w całości.

## JSON

`hydra::json` w `util/Json.hpp`: `JsonWriter` pisze strumieniowo do bufora
wołającego, `JsonView` czyta bez kopiowania. Zakres jest dokładnie taki, jaki
wynika z protokołu. Powstał, bo ArduinoJson alokuje i wymaga Arduino,
a nlohmann wymaga wyjątków i STL-a.

## Czego nie ma

Bufora ponownych prób na magistrali — nieudana wysyłka jest zgłaszana, a co
z tym zrobić, wie aplikacja. Szyfrowania na łączu szeregowym — ruch w budynku
idzie po skrętce, a nie po internecie; do internetu jest `wss://`.
Fragmentacji ramek na magistrali — wiadomość większa niż MTU oznacza za dużo
encji w jednym `hello`, a nie potrzebę dzielenia pakietów.
