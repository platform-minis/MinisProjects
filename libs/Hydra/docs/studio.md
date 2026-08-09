# Hydra Studio

Wtyczka edytora, która otwiera pliki `.hydra` w interfejsie zamiast w zwykłym
edytorze tekstu. Kod mieszka w `MyCastle/packages/hydra-studio` — jest wtyczką
tamtejszego edytora, tak samo jak MinisLib Graph.

Plik pozostaje tekstem: da się go poprawić ręcznie, obejrzeć w recenzji zmian
i scalić. Formularz i zakładka tekstowa patrzą na **ten sam model Monaco**,
więc zmiana z jednej strony jest natychmiast widoczna z drugiej.

## Podpięcie

```tsx
import { createHydraStudioPlugin } from '@mhersztowski/hydra-studio';
import * as monaco from 'monaco-editor';

const hydraPlugin = useMemo(() => createHydraStudioPlugin({
  models: {
    getModel: (uri) => monaco.editor.getModels()
      .find((m) => m.uri.toString() === uri || m.uri.path === uri),
  },
}), []);

<TextEditorWorkspace extraPlugins={[hydraPlugin]} … />
```

Opcjonalnie: `loadPacks`, `loadSchematic`, `runBuild`, `downloadArtifact`,
`openSerial`, `sendToDevice`, `onSaveVcd`, `runHilSuite`. Wtyczka nie dotyka dysku ani
Dockera sama — przeglądarka nie ma do nich dostępu, a budowa ma już własne
wejście (`docker/hydra.sh`).

## Wejścia pakietu

| Wejście | Zawartość | Wymaga |
|---|---|---|
| `.` | wtyczka edytora + cały model | React, MUI, Monaco |
| `./model` | model, walidacja, generatory, symulacja | nic |
| `./panels` | panele do osadzenia we własnym układzie | React, MUI |
| `hydra` (bin) | wiersz poleceń | Node |

`./model` nie dotyka Reacta ani systemu plików: ten sam kod działa
w przeglądarce i w skryptach budowania.

Panele ładowane są leniwie, więc samo wczytanie wtyczki nie ciągnie Material UI
— edytor nie płaci za interfejs Studia, dopóki nikt nie otworzy `.hydra`.

## Co widać w interfejsie

**Panel projektu** — nawigacja po celach, modułach i układach, formularze pól.

**Biblioteka komponentów** (panel boczny) — paczki pogrupowane po tym, czym są
dla frameworka. Kliknięcie wstawia układ do pliku: dopisuje paczkę do
`dependencies` i sam układ do `hardware.components`, obie zmiany jednym krokiem
cofania. Kafelek pokazuje wynik **przed** kliknięciem:
`wstawi: baro_2 — BMP280 @ i2c0:0x77`.

**Płótno schematu** — symbole, sieci, obwódki tam, gdzie reguły elektryczne
coś zgłaszają.

**Panel dolny** — sześć zakładek: Kompilacja, Monitor, EventBus, Problemy,
Symulacja, Farma. Zajętość Flash i RAM w pasku, czytana z wyniku budowy.

## Budowa dla maszyny, na której stoi przeglądarka

Cel `mcu: native` daje program dla pulpitu, a nie wsad — i tu przeglądarka musi
wiedzieć, na czym stoi. Studio wykrywa system i architekturę, buduje dla nich
i **pobiera gotowy plik samo**: budowa, po której trzeba jeszcze szukać wyniku
w katalogu `build/`, nie jest skończona.

Obsługiwane maszyny: `win-x64`, `win-arm64`, `mac-arm64`, `mac-x64`,
`linux-x64`, `linux-arm64`.

Wykryta maszyna stoi na pasku stanu, po prawej. Znak zapytania i pomarańczowy
kolor oznaczają, że architektura została **zgadnięta** — poprawia się to
poleceniem „Buduj cel native dla: …" z palety. Zgadywanie jest widoczne celowo, bo bywa
nieuniknione:

| Przeglądarka | Co podaje | Co z tym robimy |
|---|---|---|
| Chrome / Edge | Client Hints z architekturą | wynik pewny |
| Windows on ARM, bez Client Hints | „Win64; x64" — celowo, dla zgodności | zgadujemy x64 |
| Safari na Apple Silicon | „Intel Mac OS X" | nazwa GPU z WebGL („Apple M…") |
| Firefox na macOS | „Intel Mac OS X" | zgadujemy x64 |

Kierunek zgadywania nie jest przypadkowy: binarka x64 uruchomi się na ARM-ie
przez emulację (Rosetta 2, warstwa x64 w Windows on ARM), a arm64 na maszynie
x64 nie uruchomi się wcale. Wybieramy więc pomyłkę odwracalną.

Gospodarz podpina to opcjami `runBuild` (dostaje `hostPlatform` w żądaniu)
i opcjonalnie `downloadArtifact`, gdy chce zapisać plik po swojemu zamiast
pobierać go przeglądarką.

## Trzy decyzje, które warto znać

### Formularz wychodzi ze schematu

Inspektor nie ma własnej listy pól. Powstaje z tego samego opisu, z którego
działa walidacja — nowe ustawienie w formacie pojawia się w interfejsie samo,
wraz z opisem, listą dozwolonych wartości i zakresem. Druga, ręcznie
utrzymywana lista rozminęłaby się ze schematem przy pierwszej zmianie.

Formularze konfiguracji układów pochodzą z `config_schema` paczki, bo to autor
sterownika wie, jakie ma nadpróbkowanie i filtr.

### Zapis nie przepisuje pliku

Zmiana wartości idzie do Monaco jako **przedział tekstu**, nie jako nowa treść:

* zmienia się dokładnie jeden wiersz,
* komentarze, kolejność kluczy i wyrównanie zostają nietknięte,
* cofanie działa krok po kroku,
* gdy treść w edytorze zmieniła się w międzyczasie, zapis jest **odrzucany**
  i pole to pokazuje — przedziały wskazywałyby wtedy nie to miejsce.

Kuszące byłoby wczytać plik do struktury i zapisać całość z powrotem, ale
każdy taki zapis przechodzi przez serializator, który normalizuje formatowanie.
Po jednym kliknięciu historia zmian pokazywałaby przebudowany plik zamiast
jednej poprawki.

### Symulacja jest odtwarzalna

Prędkość mnoży liczbę kroków na klatkę, a nie ich wielkość: przy 10× nie
liczymy szybciej, tylko wykonujemy dziesięć razy więcej kroków. Przebieg jest
identyczny niezależnie od obciążenia przeglądarki, a ten sam czas i ziarno
zawsze dają tę samą wartość. Symulacja, której nie da się powtórzyć, nie nadaje
się do szukania błędów — a po to głównie istnieje.

Modele są funkcjami czasu, więc symulację da się przewinąć w dowolne miejsce
bez odtwarzania historii.

## Czego edytor nie udostępnia

Projekt interfejsu pokazuje pasek menu „Plik / Edycja / Projekt / Symulacja",
ale host nie ma punktu rozszerzenia dla paska menu — udostępnia pasek narzędzi,
paletę poleceń i pasek stanu. Polecenia trafiają więc tam, pod kategorią
„Hydra", z nazwami takimi jak w projekcie.

## Stan

Zweryfikowane: typy pakietu i aplikacji, 255 testów, budowa pakietu, budowa
aplikacji z wtyczką w środku, podział na leniwe porcje. Cel `native`
przechodzi całą drogę end-to-end: `.hydra` → generowanie → preset CMake →
zbudowana i uruchomiona binarka (sprawdzone na `linux-x64`).

**Nie zweryfikowane: zachowanie w oknie przeglądarki.** Czy `openEditorTab`
faktycznie pokaże panel po otwarciu `.hydra` i czy hooki działają, sprawdzi
dopiero uruchomienie aplikacji i wejście na stronę drive.
