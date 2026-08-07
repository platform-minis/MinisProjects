# Szablon projektu Hydra

Punkt wyjścia dla nowego urządzenia. Zawiera moduł aplikacji z taskiem
okresowym, logowanie i shell diagnostyczny — czyli minimum, od którego
zaczyna się każdy projekt.

## Uruchomienie

```bash
cp -r libs/Hydra/templates/starter moj-projekt
cd moj-projekt
pio run -e esp32s3 -t upload
pio device monitor
```

W monitorze:

```
hydra> help
hydra> ps
hydra> i2c scan
```

## Co dalej

1. **Zmień nazwę urządzenia** w `src/main.cpp` — trafia do logów, telemetrii i mDNS.
2. **Wskaż swoją płytkę**: `HYDRA_BOARD_HEADER` w `platformio.ini`. Dla własnego
   układu skopiuj plik z `include/hydra/boards/` i dopisz nazwy logiczne pinów.
3. **Włącz potrzebne moduły** — odkomentuj flagi w `platformio.ini`:

| Flaga | Co dodaje |
|---|---|
| `HYDRA_ENABLE_SENSE` | `SensorHub`, filtry, sterowniki czujników |
| `HYDRA_ENABLE_NET` | Wi-Fi, MQTT, mDNS, mostek telemetrii |
| `HYDRA_ENABLE_UI` | ekrany, widżety, wiązanie danych |
| `HYDRA_ENABLE_MOTION` | regulacja, kinematyka, łańcuch bezpieczeństwa |
| `HYDRA_ENABLE_OTA` | aktualizacja przez sieć |

Nieużywany moduł nie kosztuje ani bajta.

4. **Zobacz przykłady** w `libs/Hydra/examples/` — każdy pokazuje jeden moduł
   w działaniu i da się z nich kopiować całymi blokami.
