/**
 * Hydra — szablon modułu WebAssembly w AssemblyScript.
 *
 * Ten sam kontrakt, co skrypt Lua: opcjonalne `setup()` wołane raz i opcjonalne
 * `loop()` wołane w każdym przebiegu taska. Różnica jest w tym, co dostajesz
 * w zamian — typy sprawdzane przed wgraniem i piaskownica, z której nie da się
 * wyjść poza zadeklarowane importy.
 *
 * ## Budżet wykonania
 *
 * `loop()` dostaje budżet liczony w **krawędziach wstecznych pętli
 * i wywołaniach funkcji** (patrz `docs/wasm-imports.md` i `src/wasm3/VENDOR.md`).
 * Po jego wyczerpaniu wykonanie zostaje przerwane i **zaczyna się od nowa**
 * w kolejnym przebiegu — inaczej niż w Lua, gdzie jest wznawiane w miejscu.
 *
 * W praktyce znaczy to tyle: `loop()` ma się kończyć sama. Stan trzymaj
 * w zmiennych modułu, tak jak niżej, a nie w pętli czekającej na coś.
 */

import { millis, log, gpio_mode, gpio_write } from "./hydra";

/** Numer pinu diody. Podmień na właściwy dla swojej płytki. */
const LED: i32 = 7;

/** Tryby pinu odpowiadają `hal::PinMode`. */
const MODE_OUTPUT: i32 = 3;

/** Poziomy logu odpowiadają `hydra::LogLevel`. */
const LOG_INFO: i32 = 2;

let ledOn: bool = false;
let lastToggleMs: i32 = 0;

/**
 * Wysyła tekst do logu urządzenia.
 *
 * Napisy przekazuje się parą (offset w pamięci liniowej, długość), bo pamięć
 * modułu jest jego własną przestrzenią adresową. `String.UTF8.encode` daje
 * bufor, którego adres i rozmiar można podać wprost.
 */
function logInfo(text: string): void {
  const buffer = String.UTF8.encode(text);
  log(LOG_INFO, changetype<i32>(buffer), buffer.byteLength);
}

export function setup(): void {
  gpio_mode(LED, MODE_OUTPUT);
  lastToggleMs = millis();
  logInfo("modul wasm wystartowal");
}

export function loop(): void {
  const now = millis();

  // Odmierzanie przez porównanie czasu, a nie przez `delay()`: `loop()` ma
  // wrócić szybko, żeby budżet wystarczał i żeby task oddawał procesor.
  if (now - lastToggleMs < 500) return;

  lastToggleMs = now;
  ledOn = !ledOn;
  gpio_write(LED, ledOn ? 1 : 0);
}
