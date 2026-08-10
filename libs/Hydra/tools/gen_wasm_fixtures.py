#!/usr/bin/env python3
"""Hydra — generator modułów WebAssembly dla testów silnika WASM.

Testy nie mogą wymagać toolchaina. Tak samo jak `test/arduino_stub/` podstawia
nagłówki Arduino zamiast żądać instalacji płytki, tak tutaj moduły `.wasm`
powstają z tego skryptu i lądują w teście jako tablice bajtów. Dzięki temu
`make` w `test/` działa na maszynie, na której nie ma ani clanga z backendem
wasm32, ani wabt, ani Emscriptena.

Format binarny WebAssembly jest na tyle prosty, że złożenie kilku sekcji wprost
jest krótsze i czytelniejsze niż wciąganie zależności — a przy okazji test
sprawdza dokładnie te bajty, które chcemy sprawdzić, bez pośrednictwa
optymalizatora.

    tools/gen_wasm_fixtures.py > test/wasm_fixtures.inc

Referencja formatu: https://webassembly.github.io/spec/core/binary/
"""

import sys

# --- prymitywy kodowania ---------------------------------------------------


def uleb(value: int) -> bytes:
    """Liczba bez znaku w kodowaniu LEB128 — tak WebAssembly zapisuje długości."""
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def sleb(value: int) -> bytes:
    """Liczba ze znakiem w LEB128 — używana przez `i32.const`."""
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        done = (value == 0 and not (byte & 0x40)) or (value == -1 and (byte & 0x40))
        out.append(byte if done else byte | 0x80)
        if done:
            return bytes(out)


def vec(items) -> bytes:
    """Wektor: liczba elementów, potem elementy."""
    return uleb(len(items)) + b"".join(items)


def section(sid: int, payload: bytes) -> bytes:
    return bytes([sid]) + uleb(len(payload)) + payload


def name(text: str) -> bytes:
    raw = text.encode("utf-8")
    return uleb(len(raw)) + raw


# --- typy i opkody ---------------------------------------------------------

I32 = 0x7F
FUNC = 0x60

SEC_TYPE, SEC_IMPORT, SEC_FUNC, SEC_MEMORY, SEC_EXPORT, SEC_CODE = 1, 2, 3, 5, 7, 10

OP_BLOCK, OP_LOOP, OP_BR, OP_BR_IF, OP_END = 0x02, 0x03, 0x0C, 0x0D, 0x0B
OP_CALL = 0x10
OP_LOCAL_GET, OP_LOCAL_SET, OP_GLOBAL_GET = 0x20, 0x21, 0x23
OP_I32_CONST, OP_I32_ADD, OP_I32_LT_S, OP_DROP = 0x41, 0x6A, 0x48, 0x1A
OP_MEMORY_GROW = 0x40
VOID_BLOCK = 0x40


def functype(params, results) -> bytes:
    return bytes([FUNC]) + vec([bytes([p]) for p in params]) + vec([bytes([r]) for r in results])


def code_body(locals_, instructions: bytes) -> bytes:
    """Ciało funkcji: deklaracje zmiennych lokalnych i kod, zakończone `end`."""
    decl = vec([uleb(count) + bytes([typ]) for count, typ in locals_])
    body = decl + instructions + bytes([OP_END])
    return uleb(len(body)) + body


def module(types, imports, funcs, exports, codes, memory_pages=1) -> bytes:
    out = bytearray(b"\x00asm\x01\x00\x00\x00")
    out += section(SEC_TYPE, vec(types))
    if imports:
        out += section(SEC_IMPORT, vec(imports))
    out += section(SEC_FUNC, vec([uleb(i) for i in funcs]))
    if memory_pages is not None:
        # limits: 0x00 = tylko minimum, bez maksimum
        out += section(SEC_MEMORY, vec([bytes([0x00]) + uleb(memory_pages)]))
    out += section(SEC_EXPORT, vec(exports))
    out += section(SEC_CODE, vec(codes))
    return bytes(out)


def import_func(mod: str, fn: str, type_index: int) -> bytes:
    return name(mod) + name(fn) + bytes([0x00]) + uleb(type_index)


def export_func(fn: str, func_index: int) -> bytes:
    return name(fn) + bytes([0x00]) + uleb(func_index)


def export_mem(label: str, index: int = 0) -> bytes:
    return name(label) + bytes([0x02]) + uleb(index)


# --- moduły testowe --------------------------------------------------------


def mod_counter() -> bytes:
    """`setup`/`loop` zliczające wywołania przez import `gpio_toggle`.

    Najprostszy dowód na to, że droga moduł → import → HAL działa w obie strony:
    każde `loop()` przełącza pin, więc atrapa HAL widzi dokładnie tyle zmian,
    ile było przebiegów.
    """
    t_void = functype([], [])
    t_i_i = functype([I32], [I32])

    imports = [import_func("hydra", "gpio_toggle", 1)]

    # func 0 = import; func 1 = setup; func 2 = loop
    setup = code_body([], bytes([OP_I32_CONST]) + sleb(7) + bytes([OP_CALL]) + uleb(0) + bytes([OP_DROP]))
    loop = code_body([], bytes([OP_I32_CONST]) + sleb(7) + bytes([OP_CALL]) + uleb(0) + bytes([OP_DROP]))

    return module(
        types=[t_void, t_i_i],
        imports=imports,
        funcs=[0, 0],
        exports=[export_func("setup", 1), export_func("loop", 2), export_mem("memory")],
        codes=[setup, loop],
    )


def mod_infinite_loop() -> bytes:
    """`loop()` z pętlą bez wyjścia — sprawdzian budżetu wykonania.

    To jest najważniejszy moduł w całym zestawie. Bez łatki budżetu w wasm3
    wykonanie tego modułu nie wraca nigdy i kładzie task na zawsze.
    """
    t_void = functype([], [])

    # loop $l  br $l  end  — krawędź wsteczna, czyli miejsce poboru budżetu
    body = bytes([OP_LOOP, VOID_BLOCK, OP_BR]) + uleb(0) + bytes([OP_END])
    loop = code_body([], body)

    return module(
        types=[t_void],
        imports=[],
        funcs=[0],
        exports=[export_func("loop", 0), export_mem("memory")],
        codes=[loop],
    )


def mod_bounded_loop() -> bytes:
    """`loop()` z pętlą o znanej liczbie obrotów — mieści się w budżecie.

    Para dla modułu wyżej: dowodzi, że budżet przepuszcza pracę skończoną,
    zamiast przerywać wszystko po równo.
    """
    t_void = functype([], [])

    # local i = 0; while (i < 10) { i += 1 }
    body = bytearray()
    body += bytes([OP_I32_CONST]) + sleb(0) + bytes([OP_LOCAL_SET]) + uleb(0)
    body += bytes([OP_BLOCK, VOID_BLOCK])
    body += bytes([OP_LOOP, VOID_BLOCK])
    body += bytes([OP_LOCAL_GET]) + uleb(0) + bytes([OP_I32_CONST]) + sleb(1) + bytes([OP_I32_ADD])
    body += bytes([OP_LOCAL_SET]) + uleb(0)
    body += bytes([OP_LOCAL_GET]) + uleb(0) + bytes([OP_I32_CONST]) + sleb(10) + bytes([OP_I32_LT_S])
    body += bytes([OP_BR_IF]) + uleb(0)
    body += bytes([OP_END])  # loop
    body += bytes([OP_END])  # block

    loop = code_body([(1, I32)], bytes(body))

    return module(
        types=[t_void],
        imports=[],
        funcs=[0],
        exports=[export_func("loop", 0), export_mem("memory")],
        codes=[loop],
    )


def mod_no_loop() -> bytes:
    """Moduł bez `loop()` — sam `setup()`. Odpowiednik skryptu Lua na zdarzeniach."""
    t_void = functype([], [])
    setup = code_body([], b"")
    return module(
        types=[t_void],
        imports=[],
        funcs=[0],
        exports=[export_func("setup", 0), export_mem("memory")],
        codes=[setup],
    )


def mod_missing_import() -> bytes:
    """Moduł żądający importu spoza wystawionej powierzchni.

    Grupa `i2c` nie jest w WASM wystawiona, bo oddaje dane o kształcie tabeli.
    Moduł, który jej żąda, ma się **nie wczytać** z jasnym błędem, zamiast
    działać połowicznie.
    """
    t_void = functype([], [])
    t_i_i = functype([I32], [I32])
    imports = [import_func("hydra", "i2c_scan", 1)]
    loop = code_body([], bytes([OP_I32_CONST]) + sleb(0) + bytes([OP_CALL]) + uleb(0) + bytes([OP_DROP]))
    return module(
        types=[t_void, t_i_i],
        imports=imports,
        funcs=[0],
        exports=[export_func("loop", 1), export_mem("memory")],
        codes=[loop],
    )


FIXTURES = [
    ("kWasmCounter", mod_counter,
     "setup/loop przelaczajace pin przez import gpio_toggle"),
    ("kWasmInfiniteLoop", mod_infinite_loop,
     "loop() z petla bez wyjscia — sprawdzian budzetu"),
    ("kWasmBoundedLoop", mod_bounded_loop,
     "loop() z petla o dziesieciu obrotach — miesci sie w budzecie"),
    ("kWasmNoLoop", mod_no_loop,
     "modul z samym setup(), bez loop()"),
    ("kWasmMissingImport", mod_missing_import,
     "modul zadajacy importu spoza wystawionej powierzchni"),
]


def emit(out) -> None:
    out.write("// Wygenerowane przez tools/gen_wasm_fixtures.py — nie edytuj ręcznie.\n")
    out.write("//\n")
    out.write("// Moduły WebAssembly dla testów silnika WASM, wpisane jako bajty, żeby\n")
    out.write("// `make` w test/ nie wymagał toolchaina — ta sama zasada, co przy\n")
    out.write("// test/arduino_stub/.\n\n")

    for symbol, builder, description in FIXTURES:
        data = builder()
        out.write(f"/** {description} ({len(data)} B) */\n")
        out.write(f"const unsigned char {symbol}[] = {{\n")
        for i in range(0, len(data), 12):
            chunk = ", ".join(f"0x{b:02X}" for b in data[i:i + 12])
            out.write(f"    {chunk},\n")
        out.write("};\n\n")


if __name__ == "__main__":
    emit(sys.stdout)
