#pragma once
/*
 * Hydra — punkt macierzy konfiguracji WAMR, który osadzamy.
 *
 * Te wartości nie są preferencją, tylko **jedyną kombinacją sprawdzoną
 * kompilacją**. WAMR wiąże przełączniki ze sobą w sposób niewidoczny
 * z dokumentacji, a objawem jest błąd w pliku, którego się nie tykało:
 *
 *   WASM_ENABLE_BULK_MEMORY=1  odsłania w wasm_interp_classic.c odwołania do
 *                              `linear_mem_size`, deklarowanego dopiero przy
 *                              WASM_ENABLE_SHARED_HEAP — objaw to „undeclared”
 *                              i nieosiągalna etykieta `out_of_bounds`;
 *   WASM_ENABLE_LIBC_WASI=1    wciąga posix_clock.c, a ten libc_errno.h
 *                              z warstwy, której w tym zestawie nie ma.
 *
 * Zmiana któregokolwiek wymaga powtórzenia budowy i prawdopodobnie dołożenia
 * plików do listy w tools/vendor_wamr.sh.
 */

#define WASM_ENABLE_INTERP        1
#define WASM_ENABLE_FAST_INTERP   0   /* szybszy, ale większy — osobna decyzja */
#define WASM_ENABLE_AOT           0
#define WASM_ENABLE_JIT           0
#define WASM_ENABLE_LIBC_BUILTIN  0
#define WASM_ENABLE_LIBC_WASI     0
#define WASM_ENABLE_MULTI_MODULE  0
#define WASM_ENABLE_SHARED_MEMORY 0
#define WASM_ENABLE_THREAD_MGR    0
#define WASM_ENABLE_BULK_MEMORY   0
#define WASM_ENABLE_REF_TYPES     0
#define WASM_ENABLE_GC            0
#define WASM_ENABLE_SHARED_HEAP   0
#define WASM_ENABLE_MEMORY64      0

/*
 * Pamięć.
 *
 * WAMR ma **własny** menedżer puli (mem-alloc/ems), więc podpięcie go pod
 * pulę Hydry wygląda inaczej niż w wasm3: nie podmienia się trzech funkcji
 * alokatora, tylko podaje bufor przy `wasm_runtime_full_init()` z trybem
 * `Alloc_With_Pool`. Prostsze, ale zupełnie inne — i dlatego nie ma tu łatki
 * odpowiadającej tej z vendor_wasm3.sh.
 */
#define BH_MALLOC wasm_runtime_malloc
#define BH_FREE   wasm_runtime_free
