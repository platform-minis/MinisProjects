# WAMR — źródła osadzone

Rdzeń [WebAssembly Micro Runtime](https://github.com/bytecodealliance/wasm-micro-runtime),
commit `97c7b8fd30b309abfe3a60b86bc5abb112fedbfa`, licencja Apache-2.0 WITH LLVM-exception.

Odtwarza to drzewo `tools/vendor_wamr.sh`. Osadzony jest **jeden** punkt
macierzy konfiguracji: klasyczny interpreter, bez WASI, wątków i AOT.
Wartości w `hydra_wamr_conf.h` są sprawdzone kompilacją.

## Stan weryfikacji

| Element | Stan |
|---|---|
| Kompilacja rdzenia na Linuksie (aarch64) | sprawdzona: 32/32 plików, ~220 KB kodu |
| Port `esp-idf` / `freertos` | skopiowany, **nieuruchomiony** |
| `WamrEngine` za `IScriptEngine` | **nie istnieje** |
| Podpięcie pod pulę `script::Heap` | **nie zrobione** |

Drzewo nie jest jeszcze wciągane przez żaden system budowania —
`HYDRA_SCRIPT_HAS_WAMR` zostaje zerem, a `EngineSelector` cofa się do wasm3
z zaznaczonym ustępstwem.

## Budowa rdzenia — polecenie sprawdzone

    ARCH=$(uname -m)
    case $ARCH in aarch64) T=-DBUILD_TARGET_AARCH64; N=aarch64;;
                   *)       T=-DBUILD_TARGET_X86_64;  N=em64;; esac

    gcc -std=gnu99 -w -c $T -DBH_PLATFORM_LINUX -include hydra_wamr_conf.h \
        -Iinclude -Iiwasm/include -Iiwasm/common -Iiwasm/interpreter \
        -Ishared/utils -Ishared/platform/include -Ishared/platform/linux \
        -Ishared/mem-alloc -Ishared/mem-alloc/ems \
        iwasm/interpreter/*.c iwasm/common/wasm_*.c \
        iwasm/common/arch/invokeNative_$N.s \
        shared/utils/*.c shared/mem-alloc/*.c shared/mem-alloc/ems/*.c \
        shared/platform/linux/platform_init.c \
        shared/platform/common/posix/posix_{thread,time,malloc,memmap,sleep,blocking_op}.c

Uwaga: `posix_clock.c` jest **pominięty** świadomie — wciąga `libc_errno.h`
z warstwy WASI, której tu nie ma.
