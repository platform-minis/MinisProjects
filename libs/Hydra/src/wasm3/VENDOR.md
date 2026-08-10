# wasm3 — źródła osadzone

Kopia rdzenia interpretera [wasm3](https://github.com/wasm3/wasm3),
commit `d77cd814aa0bc68cb1df917580a6304d34cfb30b`, licencja MIT.

Drzewo **nie jest** edytowane ręcznie. Odtwarza je `tools/vendor_wasm3.sh`,
który pobiera źródła i nakłada trzy łatki:

1. `hydra_m3_conf.h` wpięty w `m3_config.h` — rozmiar strony kodu, wysokość
   stosu funkcji, wyłączone WASI i tracer.
2. Alokator w `m3_core.c` skierowany na pulę `script::Heap` pod
   `HYDRA_WASM3_ALLOC` — bez tego wasm3 woła `malloc()`.
3. Ten plik.

Pominięte wobec oryginału: `m3_api_wasi.c`, `m3_api_uvwasi.c`,
`m3_api_meta_wasi.c`, `m3_api_libc.c`, `m3_api_tracer.c`. Powody są
w komentarzu przy liście `KEEP` w skrypcie osadzającym.

Podniesienie wersji: zmień `WASM3_COMMIT` i uruchom skrypt. Jeśli łatka się
nie nałoży, skrypt przerwie z komunikatem wskazującym miejsce.
