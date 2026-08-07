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

Opcjonalnie: `loadPacks`, `loadSchematic`, `runBuild`, `openSerial`,
`sendToDevice`, `onSaveVcd`, `runHilSuite`. Wtyczka nie dotyka dysku ani
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

Zweryfikowane: typy pakietu i aplikacji, 228 testów, budowa pakietu, budowa
aplikacji z wtyczką w środku, podział na leniwe porcje.

**Nie zweryfikowane: zachowanie w oknie przeglądarki.** Czy `openEditorTab`
faktycznie pokaże panel po otwarciu `.hydra` i czy hooki działają, sprawdzi
dopiero uruchomienie aplikacji i wejście na stronę drive.
