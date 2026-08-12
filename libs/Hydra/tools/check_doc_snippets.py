#!/usr/bin/env python3
"""
Hydra — pokrycie fragmentów kodu z dokumentacji.

`make -C test docs` kompiluje `docs/snippets.cpp` i dzięki temu wyłapuje
rozjazd między dokumentacją a API: przemianowane pole albo usunięta metoda
wywalają budowę. Ma to jednak dziurę — fragment **dopisany do dokumentu**
i nieprzeniesiony do lustra nie jest sprawdzany przez nikogo i cicho starzeje
się razem z kodem.

Ten skrypt tę dziurę zamyka. Dla każdego bloku ```cpp w dokumentacji sprawdza,
czy w lustrze występuje choć jeden charakterystyczny identyfikator z bloku.
Nie jest to dowód zgodności co do znaku — jest to wykrywacz fragmentów,
o których lustro nigdy nie słyszało.

Bloki, które kodem nie są — pseudokod, szkic struktury, wzór — oznacza się
w dokumencie komentarzem HTML tuż nad płotkiem:

    <!-- nie-kompilowany: wzór, nie kod -->
    ```cpp
    budget = (nowUs - lastUs + carry) * sampleRate / 1e6;
    ```

Komentarz jest niewidoczny w renderowanym dokumencie, a w źródle mówi wprost,
że tego fragmentu nikt nie sprawdza — i dlaczego.

Uruchomienie:  tools/check_doc_snippets.py   (albo przez `make -C test docs`)
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MIRROR = ROOT / "docs" / "snippets.cpp"

# Dokumenty z lustrem. Pozostałe (build.md, boards.md…) opisują wywołania
# powłoki i pliki konfiguracyjne, więc nie mają czego lustrzać.
DOCS = ["api.md", "media.md", "minis.md", "net.md"]

OPT_OUT = re.compile(r"<!--\s*nie-kompilowany:")
FENCE = re.compile(r"```cpp\n(.*?)```", re.S)

# Identyfikator na tyle długi, żeby jego zbieżność nie była przypadkiem.
TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]{5,}")

# Słowa języka i typy podstawowe: występują wszędzie, więc nie świadczą o niczym.
NOISE = {
    "return", "static", "struct", "public", "private", "namespace", "template",
    "constexpr", "override", "virtual", "nullptr", "include", "define", "size_t",
    "unsigned", "typedef", "sizeof", "auto_", "delete", "switch", "default",
}


def blocks_of(text):
    """Bloki ```cpp wraz z informacją, czy są wyłączone ze sprawdzania."""
    for match in FENCE.finditer(text):
        before = text[: match.start()].rstrip().rsplit("\n", 1)
        preceding = before[-1] if before else ""
        yield match.group(1), bool(OPT_OUT.search(preceding))


def main():
    if not MIRROR.exists():
        print(f"BŁĄD: brak lustra {MIRROR}", file=sys.stderr)
        return 2

    mirror_text = MIRROR.read_text(encoding="utf-8")
    problems = []
    checked = skipped = 0

    for name in DOCS:
        path = ROOT / "docs" / name
        if not path.exists():
            problems.append(f"{name}: dokument nie istnieje, a jest na liście")
            continue

        for index, (code, opted_out) in enumerate(blocks_of(path.read_text(encoding="utf-8"))):
            if opted_out:
                skipped += 1
                continue
            checked += 1

            tokens = {t for t in TOKEN.findall(code) if t not in NOISE}
            if not tokens:
                # Blok bez żadnego identyfikatora dłuższego niż pięć znaków
                # nie ma się z czym zestawić; nie zgadujemy.
                continue

            if not any(t in mirror_text for t in tokens):
                first = next((l.strip() for l in code.splitlines() if l.strip()), "")
                problems.append(
                    f"{name} blok #{index}: żaden identyfikator nie występuje "
                    f"w snippets.cpp\n      {first[:70]}"
                )

    if problems:
        print("\033[31m✗ fragmenty dokumentacji bez odpowiednika w docs/snippets.cpp\033[0m")
        for problem in problems:
            print(f"    {problem}")
        print("\n  Dopisz fragment do docs/snippets.cpp albo oznacz go w dokumencie:")
        print("      <!-- nie-kompilowany: powód -->")
        return 1

    print(f"\033[32m✓\033[0m pokrycie fragmentów: {checked} sprawdzonych, "
          f"{skipped} oznaczonych jako nie-kompilowane")
    return 0


if __name__ == "__main__":
    sys.exit(main())
