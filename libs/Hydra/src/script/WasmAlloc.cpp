/**
 * Hydra — alokacja pamięci osadzonego wasm3 na puli frameworka.
 *
 * Trzy funkcje mostkujące `m3_Malloc`/`m3_FreeImpl`/`m3_Realloc` na
 * `script::Heap`. Cała logika alokacji siedzi w `Heap`, tutaj jest wyłącznie
 * przekład i zerowanie, którego wasm3 oczekuje po `calloc`.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_SCRIPT_ENGINE_WASM

#include "hydra/script/WasmAlloc.h"

#include <string.h>

#include "hydra/script/Heap.hpp"

namespace hydra {
namespace script {
namespace detail {

/**
 * Aktywna pula wasm3.
 *
 * Wskaźnik statyczny, bo wasm3 alokuje przez funkcje bez kontekstu i nie ma
 * gdzie przekazać sterty — inaczej niż `lua_Alloc`, które dostaje wskaźnik
 * użytkownika. Ustawia go `WasmEngine::open()`, zdejmuje `close()`.
 */
Heap* gWasmHeap = nullptr;

}  // namespace detail
}  // namespace script
}  // namespace hydra

using hydra::script::detail::gWasmHeap;

extern "C" {

void* hydraWasm3Alloc(size_t bytes) {
    if (gWasmHeap == nullptr || bytes == 0) return nullptr;

    void* ptr = gWasmHeap->allocate(bytes);
    // wasm3 zakłada semantykę `calloc`: struktury kompilacji trzymają wskaźniki,
    // które muszą startować wyzerowane, bo ścieżka błędu zwalnia je warunkowo.
    if (ptr != nullptr) memset(ptr, 0, bytes);
    return ptr;
}

void hydraWasm3Free(void* ptr) {
    if (gWasmHeap == nullptr || ptr == nullptr) return;
    gWasmHeap->release(ptr);
}

void* hydraWasm3Realloc(void* ptr, size_t oldBytes, size_t newBytes) {
    if (gWasmHeap == nullptr) return nullptr;
    if (newBytes == 0) {
        gWasmHeap->release(ptr);
        return nullptr;
    }

    void* fresh = gWasmHeap->reallocate(ptr, oldBytes, newBytes);
    if (fresh != nullptr && newBytes > oldBytes) {
        memset(static_cast<hydra::u8*>(fresh) + oldBytes, 0, newBytes - oldBytes);
    }
    return fresh;
}

}  // extern "C"

#endif  // HYDRA_SCRIPT_ENGINE_WASM
