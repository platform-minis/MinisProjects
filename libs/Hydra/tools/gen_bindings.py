#!/usr/bin/env python3
"""Hydra — generator powierzchni importów WebAssembly.

Czyta `tools/wasm_bindings.def` i wytwarza z niego trzy pliki, które inaczej
musiałyby być pisane ręcznie w dwóch językach i dwóch repozytoriach:

    src/script/wasm_imports.inc          tablice rejestracyjne dla C++
    templates/assemblyscript/assembly/hydra.ts   deklaracje dla AssemblyScript
    docs/wasm-imports.md                 tabela do dokumentacji

Typy AssemblyScript wyprowadzamy z sygnatury wasm3, zamiast zapisywać osobno —
inaczej rozjechałyby się w obrębie jednego pliku, co byłoby już zupełnie
niewybaczalne.

    tools/gen_bindings.py           # wygeneruj
    tools/gen_bindings.py --check   # sprawdź, czy wygenerowane pliki są aktualne

Tryb `--check` istnieje dla CI: zmiana `.def` bez regeneracji ma zatrzymać
budowę, a nie zostać zauważona przy najbliższym błędzie linkowania modułu.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEF_FILE = ROOT / "tools" / "wasm_bindings.def"

OUT_CPP = ROOT / "src" / "script" / "wasm_imports.inc"
OUT_DTS = ROOT / "templates" / "assemblyscript" / "assembly" / "hydra.ts"
OUT_DOC = ROOT / "docs" / "wasm-imports.md"

BANNER_CPP = "// Wygenerowane przez tools/gen_bindings.py z tools/wasm_bindings.def.\n// Nie edytuj ręcznie — zmiany przepadną przy regeneracji.\n"
BANNER_TS = "// Wygenerowane przez tools/gen_bindings.py z tools/wasm_bindings.def.\n// Nie edytuj ręcznie — zmiany przepadną przy regeneracji.\n"

# Typy wasm3 → AssemblyScript. `i` jest w AS liczbą 32-bitową ze znakiem;
# tam, gdzie host traktuje ją bez znaku, mówi o tym opis funkcji.
AS_TYPES = {"v": "void", "i": "i32", "I": "i64", "f": "f32", "F": "f64"}


class Entry:
    def __init__(self, group, name, signature, cpp, params, doc):
        self.group = group
        self.name = name
        self.signature = signature
        self.cpp = cpp
        self.doc = doc

        self.result, self.args = parse_signature(signature, name)
        self.params = split_params(params, len(self.args), name)

    def as_declaration(self) -> str:
        args = ", ".join(f"{p}: {AS_TYPES[a]}" for p, a in zip(self.params, self.args))
        return f"export declare function {self.name}({args}): {AS_TYPES[self.result]};"


def parse_signature(signature: str, name: str):
    """`i(ii)` → ('i', ['i', 'i'])."""
    if "(" not in signature or not signature.endswith(")"):
        sys.exit(f"{name}: sygnatura '{signature}' nie ma postaci wynik(argumenty)")
    result, _, rest = signature.partition("(")
    args = rest[:-1]
    for ch in (result + args):
        if ch not in AS_TYPES:
            sys.exit(f"{name}: nieznany typ '{ch}' w sygnaturze '{signature}'")
    if len(result) != 1:
        sys.exit(f"{name}: sygnatura '{signature}' musi mieć dokładnie jeden wynik")
    return result, list(args)


def split_params(raw: str, count: int, name: str):
    names = [p.strip() for p in raw.split(",") if p.strip()]
    if len(names) != count:
        sys.exit(f"{name}: sygnatura ma {count} argumentów, a nazw podano {len(names)}")
    return names


def load() -> list:
    entries = []
    for lineno, line in enumerate(DEF_FILE.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        parts = [p.strip() for p in stripped.split("|")]
        if len(parts) != 6:
            sys.exit(f"{DEF_FILE.name}:{lineno}: oczekiwano 6 pól, jest {len(parts)}")
        entries.append(Entry(*parts))
    return entries


def groups(entries):
    """Grupy w kolejności pierwszego wystąpienia — kolejność w pliku jest celowa."""
    out = {}
    for e in entries:
        out.setdefault(e.group, []).append(e)
    return out


# --- generatory -------------------------------------------------------------


def render_cpp(entries) -> str:
    out = [BANNER_CPP, "\n"]
    for group, items in groups(entries).items():
        out.append(f"const Import k{group.capitalize()}Imports[] = {{\n")
        width = max(len(e.name) for e in items) + 4
        sigw = max(len(e.signature) for e in items) + 4
        for e in items:
            n = f'"{e.name}",'
            s = f'"{e.signature}",'
            out.append(f"    {{{n:<{width}}{s:<{sigw}}{e.cpp}}},\n")
        out.append("};\n\n")

    out.append("/** Grupa importów wraz z liczebnością — do pętli linkującej. */\n")
    out.append("struct ImportGroup {\n")
    out.append("    const Import* imports;\n")
    out.append("    size_t        count;\n")
    out.append("    bool BindingSet::*flag;\n")
    out.append("};\n\n")
    out.append("const ImportGroup kImportGroups[] = {\n")
    for group, items in groups(entries).items():
        table = f"k{group.capitalize()}Imports"
        out.append(f"    {{{table}, {len(items)}, &BindingSet::{group}}},\n")
    out.append("};\n")
    return "".join(out)


def render_dts(entries) -> str:
    out = [BANNER_TS, "\n"]
    out.append("//\n")
    out.append("// Deklaracje importów Hydry dla modułów pisanych w AssemblyScript.\n")
    out.append("//\n")
    out.append("// Wszystkie funkcje leżą w module `hydra`. Moduł deklaruje tylko te,\n")
    out.append("// których naprawdę używa — grupa niewłączona w `BindingSet` po stronie\n")
    out.append("// urządzenia nie zostaje zlinkowana i moduł jej żądający się nie wczyta.\n")
    out.append("//\n")
    out.append("// Napisy przekazuje się parą (offset, długość): pamięć modułu jest jego\n")
    out.append("// własną przestrzenią adresową.\n")
    out.append("\n")

    for group, items in groups(entries).items():
        out.append(f"// --- {group} " + "-" * (68 - len(group)) + "\n\n")
        for e in items:
            out.append(f"/** {e.doc} */\n")
            out.append(f'@external("hydra", "{e.name}")\n')
            out.append(e.as_declaration() + "\n\n")
    return "".join(out)


def render_doc(entries) -> str:
    out = ["<!-- Wygenerowane przez tools/gen_bindings.py. Nie edytuj ręcznie. -->\n\n"]
    out.append("# Powierzchnia importów WebAssembly\n\n")
    out.append("Wszystko, co moduł WebAssembly widzi z urządzenia. Lista powstaje\n")
    out.append("z `tools/wasm_bindings.def` — tego samego pliku, z którego generują się\n")
    out.append("tablice w C++ i deklaracje dla AssemblyScript, więc nie ma jak się\n")
    out.append("rozjechać.\n\n")
    out.append("Grupa niewłączona w `BindingSet` nie zostaje zlinkowana. Moduł, który jej\n")
    out.append("żąda, **nie wczyta się** — z jasnym błędem, zamiast działać połowicznie.\n\n")

    for group, items in groups(entries).items():
        out.append(f"## `{group}`\n\n")
        out.append("| Funkcja | Sygnatura AssemblyScript | Opis |\n")
        out.append("|---|---|---|\n")
        for e in items:
            args = ", ".join(f"{p}: {AS_TYPES[a]}" for p, a in zip(e.params, e.args))
            sig = f"`{e.name}({args}): {AS_TYPES[e.result]}`"
            out.append(f"| `{e.name}` | {sig} | {e.doc} |\n")
        out.append("\n")

    out.append("## Eksporty, których host szuka w module\n\n")
    out.append("| Eksport | Sygnatura | Kiedy wołany |\n")
    out.append("|---|---|---|\n")
    out.append("| `setup` | `(): void` | raz, po wczytaniu modułu |\n")
    out.append("| `loop` | `(): void` | w każdym przebiegu taska, z budżetem |\n")
    out.append("| `on_event` | `(nameId: i32, value: f32, data: i32): void` | dla każdego sygnału z magistrali |\n")
    out.append("\nWszystkie trzy są opcjonalne. `on_event` zastępuje `hydra.event.on`\n")
    out.append("z Lua: WebAssembly nie ma domknięć, które dałoby się zarejestrować\n")
    out.append("w tabeli, więc odbiór jest eksportem, a nie wywołaniem zwrotnym.\n")
    return "".join(out)


TARGETS = [(OUT_CPP, render_cpp), (OUT_DTS, render_dts), (OUT_DOC, render_doc)]


def main() -> int:
    entries = load()
    check = "--check" in sys.argv[1:]
    stale = []

    for path, render in TARGETS:
        want = render(entries)
        if check:
            have = path.read_text(encoding="utf-8") if path.exists() else ""
            if have != want:
                stale.append(path.relative_to(ROOT))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(want, encoding="utf-8")

    if check:
        if stale:
            print("Nieaktualne pliki generowane:", file=sys.stderr)
            for p in stale:
                print(f"  {p}", file=sys.stderr)
            print("\nUruchom tools/gen_bindings.py", file=sys.stderr)
            return 1
        print(f"\033[32m✓\033[0m powierzchnia importów aktualna ({len(entries)} funkcji)")
        return 0

    print(f"\033[32m✓\033[0m wygenerowano {len(TARGETS)} pliki z {len(entries)} funkcji")
    for path, _ in TARGETS:
        print(f"    {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
