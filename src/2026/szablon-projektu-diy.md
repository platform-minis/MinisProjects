<!-- bid:eb86c6cf-f3aa-47c3-af3d-fe7ab714226f -->

# Nazwa Projektu

<!-- bid:5e6a279b-8eac-45a1-8630-1bab88c61e91 -->

> Krótki opis jednym zdaniem — co to jest i po co powstało.

<!-- bid:f10ee9c2-7623-43cb-aaad-f6dfeac5c726 -->

![Zdjęcie główne projektu](img/hero.jpg)

* * *

<!-- bid:e762f5f3-55ea-416a-bc80-eb28d6905c53 -->

## 📋 Metryka projektu

<!-- bid:84e5d3c0-fd7d-41a9-9b7d-601c19d4bae9 -->

| Pole | Wartość |
| --- | --- |
| Wersja | v1.0.0 |
| Status | 🟡 W trakcie / 🟢 Ukończony / 🔴 Wstrzymany |
| Data rozpoczęcia | 2026-01-15 |
| Data ukończenia | — |
| Czas realizacji | ~40 h (szacowany / rzeczywisty) |
| Repozytorium | github.com/user/projekt |
| Licencja | MIT / CERN-OHL / — |
| Autor | — |

* * *

<!-- bid:4a32874e-4e6b-4ee5-a06f-9d828269c84e -->

## 📖 Opis

<!-- bid:905869db-c765-46d6-879e-25db2704833e -->

Szczegółowy opis projektu:

<!-- bid:bd1957ce-f52b-4a30-a76b-01f353e83b0e -->

-   **Cel** — jaki problem rozwiązuje
    
-   **Główne funkcje** — co potrafi
    
-   **Motywacja** — dlaczego powstał
    

<!-- bid:d19226a5-27ef-44d7-91f3-cf22164094fb -->

### Kluczowe parametry

<!-- bid:6b974fd1-bd57-4699-986e-a8b251b3d73d -->

| Parametr | Wartość |
| --- | --- |
| Zasilanie | 5 V / USB-C |
| MCU | ESP32-S3 |
| Komunikacja | WiFi / BLE / LoRa |
| Pobór prądu | ~80 mA |

* * *

<!-- bid:db87b595-6d52-4e5e-b229-d3e200ab0b30 -->

## 🔩 Sprzęt (Hardware)

<!-- bid:12acdb95-dc75-4030-9b81-15964b536345 -->

### Płytki PCB

<!-- bid:fd99714d-a533-49b5-b9b4-3eff007f1b05 -->

| Nazwa | Wersja | Narzędzie | Fabrykacja | Pliki / Link |
| --- | --- | --- | --- | --- |
| Płytka główna | v1.2 | EasyEDA Pro | JLCPCB PCBA | pcb/main-board/ |
| Moduł czujnika | v1.0 | EasyEDA Pro | fotopozytyw DIY | pcb/sensor/ |

<!-- bid:f6f81cd6-3f74-40e2-a6d0-47c6b211d7d5 -->

**Uwagi do PCB:**

<!-- bid:b18a1124-608c-4e6e-9f2b-197594f5f5aa -->

-   Zmiany między wersjami (errata, poprawki)
    
-   Linki do projektów w EasyEDA / OSHWLab
    

<!-- bid:eb38faeb-a27e-4cc7-aad5-3bed00f01d2e -->

### BOM (najważniejsze komponenty)

<!-- bid:59d0c9d0-c2ad-415d-9cbc-e03eef2eb721 -->

| Komponent | Ilość | Źródło | Uwagi |
| --- | --- | --- | --- |
| ESP32-S3-WROOM-1 | 1 | LCSC | — |
| SX1262 (E22-900M22S) | 1 | TME | — |

<!-- bid:f80db796-92f3-4df6-bad1-b3684138b373 -->

Pełny BOM: `hardware/bom.csv`

<!-- bid:18a9a028-ff8c-4f00-b422-f2f4189b921a -->

### Modele 3D (obudowa i mechanika)

<!-- bid:c049235c-fbfa-4242-b58b-234b756070fd -->

| Model | Drukarka / technologia | Materiał | Pliki |
| --- | --- | --- | --- |
| Obudowa dolna | Bambu A1 Mini (FDM) | PETG | 3d/case-bottom.stl |
| Obudowa górna | Bambu A1 Mini (FDM) | PETG | 3d/case-top.stl |
| Uchwyt czujnika | Photon Mono 4 (resin) | żywica standard | 3d/sensor-mount.stl |

<!-- bid:5c198ca2-39c0-49ec-9454-24b5e9ec7bc5 -->

**Parametry druku:** warstwa 0.2 mm, wypełnienie 20%, bez supportów (lub opisz).

<!-- bid:43e5ef5c-ea3a-4ff0-b198-6c8c1fb1cfe9 -->

Źródła edytowalne (Fusion 360 / FreeCAD / OnShape): `3d/src/`

* * *

<!-- bid:5da410dc-4380-42ac-bc24-d53236f7f2e1 -->

## 💻 Oprogramowanie (Firmware / Software)

<!-- bid:07d22a57-ece8-49fa-be0e-267b2560a82e -->

-   **Framework:** ESP-IDF / Arduino / PlatformIO / Hydra
    
-   **Język:** C++ / TypeScript
    
-   **Build:** instrukcja kompilacji i flashowania
    

<!-- bid:c8e80d22-8f71-4340-a85e-3134a5ec3c1a -->

```bash
# przykład
pio run -t upload
```

<!-- bid:c6a38d8d-22f0-49b3-9c91-5ba9e451aef2 -->

Konfiguracja: `firmware/README.md`

* * *

<!-- bid:a6e844e0-4ba4-4a92-ae0a-f8bfd1d9c40c -->

## 🔗 Projekty powiązane

<!-- bid:8562b32e-b372-404c-a652-0562343beeb3 -->

| Projekt | Relacja | Link |
| --- | --- | --- |
| Nazwa projektu A | wykorzystuje wspólną bibliotekę | repo |
| Nazwa projektu B | poprzednia iteracja | repo |

* * *

<!-- bid:85efeea0-6c27-4d5c-a60a-d567aa6398e9 -->

## 📷 Galeria

<!-- bid:f4da8313-e740-4d0e-b87c-6811783869ec -->

|  |  |  |
| --- | --- | --- |
|  |  |  |
| Prototyp na płytce stykowej | PCB — strona górna | PCB — strona dolna |
|  |  |  |
| Montaż komponentów | Wydrukowana obudowa | Gotowe urządzenie |

* * *

<!-- bid:812fd938-04ce-4b5a-802b-1b5a3f0df7bd -->

## 📝 Dziennik zmian (Changelog)

<!-- bid:dd0e4422-0712-4b0e-b6e5-00ca94867c18 -->

### v1.0.0 — 2026-01-15

<!-- bid:a8a3dbef-9090-4340-80b4-3106810cee20 -->

-   Pierwsza działająca wersja
    

<!-- bid:f3176426-abc8-4d21-867c-3df70686c53b -->

### v0.2.0 — 2025-12-20

<!-- bid:05cbec1f-9acb-4761-b7eb-810ded27be8b -->

-   Poprawiona płytka PCB (v1.2), naprawiony footprint USB-C
    

<!-- bid:c538fe9a-b51e-4ff3-9182-094d6991371e -->

### v0.1.0 — 2025-12-01

<!-- bid:55b50bdb-4b8e-46c2-9fab-301077e41e0a -->

-   Prototyp na breadboardzie
    

* * *

<!-- bid:957eb2f0-18fa-4ec9-a2d7-dd427f691021 -->

## ✅ TODO / Plany rozwoju

<!-- bid:e87e72cb-7cd0-46c8-a54c-2cbd6a1b2562 -->

-   Dodanie deep sleep
    
-   Wersja z zasilaniem bateryjnym
    
-   Obudowa IP65
    

* * *

<!-- bid:8f3b2450-9266-45c2-ab45-f3128f3d6e51 -->

## 📚 Źródła i inspiracje

<!-- bid:01adebf9-9be3-4b5b-827f-471be0792b56 -->

-   [Link do datasheet / artykułu / forum](https://…)
    

<!-- bid:78597a2b-fb13-4f5c-9e23-d5376d39671f -->

&nbsp;