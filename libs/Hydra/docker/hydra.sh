#!/usr/bin/env bash
#
# Hydra — jedno wejście do środowiska budowania.
#
#   ./docker/hydra.sh build            # zbuduj obraz lokalnie
#   ./docker/hydra.sh pull             # albo pobierz gotowy z rejestru
#   ./docker/hydra.sh test             # testy hostowe (te same co w CI)
#   ./docker/hydra.sh fw esp32s3 blink-task
#   ./docker/hydra.sh fw all           # wszystkie przykłady na wszystkich płytkach
#   ./docker/hydra.sh ci               # komplet: testy + wszystkie wsady
#   ./docker/hydra.sh shell            # powłoka w kontenerze
#   ./docker/hydra.sh project ~/moj-projekt pio run -e esp32s3
#
# Działa tak samo na stanowisku programisty i na serwerze bez terminala:
# `-t` dokładane jest tylko wtedy, gdy naprawdę jest konsola, więc wywołanie
# z crona albo z runnera CI nie wywraca się na „the input device is not a TTY".
#
# Zmienne środowiskowe:
#   HYDRA_IMAGE      nazwa obrazu (domyślnie ghcr.io/platform-minis/hydra-build:latest)
#   HYDRA_PLATFORM   architektura obrazu, np. linux/amd64 — patrz uwaga niżej
#   HYDRA_ENVS       które środowiska wpiec przy `build`
#   DOCKER           polecenie zamiast `docker` (np. podman)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DOCKER="${DOCKER:-docker}"
IMAGE="${HYDRA_IMAGE:-ghcr.io/platform-minis/hydra-build:latest}"
ENVS_ALL="esp32s3 esp32c3 pico pico2 stm32g4"

die() { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }
info() { printf '\033[36m%s\033[0m\n' "$*"; }

require_daemon() {
    "$DOCKER" info >/dev/null 2>&1 || die \
"Demon Dockera nie odpowiada.
  macOS z colimą:  colima start --cpu 4 --memory 8 --disk 60
  Linux:           sudo systemctl start docker"
}

# Kontener pisze do podmontowanego drzewa źródeł. Bez --user robiłby to jako
# root i artefakty trzeba by potem odzyskiwać przez chown, a `clean` wymagałby
# sudo — dokładnie to działo się przy obrazie do pico.
docker_run() {
    local tty_flags=()
    [ -t 0 ] && [ -t 1 ] && tty_flags=(-t)

    local platform_flags=()
    [ -n "${HYDRA_PLATFORM:-}" ] && platform_flags=(--platform "$HYDRA_PLATFORM")

    # Dodatkowe opcje uruchomienia — używane przez wykrywanie wyścigów, które
    # potrzebuje wyłączyć losowanie układu pamięci.
    local extra_flags=()
    [ -n "${HYDRA_DOCKER_EXTRA:-}" ] && read -r -a extra_flags <<< "$HYDRA_DOCKER_EXTRA"

    # Zapis "${arr[@]+...}" zamiast zwykłego "${arr[@]}": macOS ma w systemie
    # basha 3.2, a tam rozwinięcie pustej tablicy przy `set -u` jest błędem,
    # nie pustką. Skrypt padał na pierwszym uruchomieniu bez terminala.
    "$DOCKER" run --rm -i \
        ${tty_flags[@]+"${tty_flags[@]}"} \
        ${platform_flags[@]+"${platform_flags[@]}"} \
        ${extra_flags[@]+"${extra_flags[@]}"} \
        --user "$(id -u):$(id -g)" \
        -v "$ROOT:/workspace" \
        -w /workspace \
        -e HOME=/tmp \
        -e TERM="${TERM:-dumb}" \
        "$IMAGE" "$@"
}

cmd_build() {
    require_daemon
    local envs="${HYDRA_ENVS:-$ENVS_ALL}"
    info "Buduję $IMAGE (środowiska: $envs)"
    info "Pierwsza budowa ściąga kilka gigabajtów toolchainów — to potrwa."
    "$DOCKER" build \
        ${HYDRA_PLATFORM:+--platform "$HYDRA_PLATFORM"} \
        --build-arg PIO_ENVS="$envs" \
        -t "$IMAGE" \
        "$ROOT/docker"
    info "Gotowe. Zawartość obrazu: ./docker/hydra.sh run cat /opt/pio/hydra-image.txt"
}

cmd_pull() {
    require_daemon
    info "Pobieram $IMAGE"
    "$DOCKER" pull ${HYDRA_PLATFORM:+--platform "$HYDRA_PLATFORM"} "$IMAGE"
}

cmd_test() {
    require_daemon
    docker_run bash -c '
        set -e
        make -C test
        make -C test asan
    '
    # Wykrywanie wyścigów wyłącza losowanie układu pamięci wywołaniem
    # personality(ADDR_NO_RANDOMIZE), a domyślny profil seccomp Dockera je
    # blokuje — bez tej opcji TSan przerywa działanie zaraz po starcie.
    HYDRA_DOCKER_EXTRA="--security-opt seccomp=unconfined --cap-add SYS_PTRACE" \
        docker_run make -C test tsan
    docker_run bash -c '
        set -e
        make -C test examples
        make -C test stub
        make -C test docs
        ./tools/check_includes.sh
    '
}

# Budowa wsadu: `fw <środowisko> <przykład>`, albo `fw all`.
cmd_fw() {
    require_daemon
    local env="${1:-all}" example="${2:-all}"

    if [ "$env" = all ]; then
        local failed=""
        for e in $ENVS_ALL; do
            cmd_fw "$e" "$example" || failed="$failed $e"
        done
        [ -z "$failed" ] || die "Niepowodzenie w środowiskach:$failed"
        return 0
    fi

    if [ "$example" = all ]; then
        for dir in "$ROOT"/examples/*/; do
            cmd_fw "$env" "$(basename "$dir")"
        done
        return 0
    fi

    [ -d "$ROOT/examples/$example" ] || die "Nie ma przykładu: $example"

    # Cała wiedza o tym, czego przykład potrzebuje, siedzi w jednym skrypcie —
    # tym samym, którego używa CI.
    docker_run tools/build_example.sh "$env" "$example"
}


cmd_ci() {
    cmd_test
    cmd_fw all all
    info "Testy hostowe i wszystkie wsady przeszły."
}

# Budowa projektu leżącego poza repozytorium Hydry — tego używa Studio pod
# „Projekt / Buduj". Montujemy projekt jako katalog roboczy, a Hydrę pod
# /hydra/Hydra: `lib_extra_dirs` wskazuje katalog **zawierający** biblioteki,
# a nie samą bibliotekę, więc podmontowanie jej wprost pod /hydra sprawiało,
# że PlatformIO jej nie widziało i konsolidacja kończyła się brakiem symboli.
cmd_project() {
    require_daemon
    local dir="${1:?podaj katalog projektu}"; shift
    [ -d "$dir" ] || die "Nie ma katalogu: $dir"
    dir="$(cd "$dir" && pwd)"

    # Maszyna wirtualna Dockera widzi tylko wybrane katalogi hosta — colima
    # domyślnie udostępnia katalog domowy. Projekt spoza tej listy montuje się
    # jako pusty katalog należący do roota, a build kończy się mylącym
    # „Path is not writable". Lepiej powiedzieć to wprost.
    if ! "$DOCKER" run --rm -v "$dir:/probe" --user "$(id -u):$(id -g)" \
            "$IMAGE" test -w /probe 2>/dev/null; then
        die "Katalog $dir nie jest zapisywalny wewnątrz kontenera.
  Najczęstsza przyczyna: leży poza katalogami udostępnionymi maszynie Dockera.
  colima udostępnia domyślnie tylko katalog domowy — przenieś projekt pod
  \$HOME albo dodaj katalog: colima start --mount \"$dir:w\""
    fi

    local tty_flags=()
    [ -t 0 ] && [ -t 1 ] && tty_flags=(-t)
    local platform_flags=()
    [ -n "${HYDRA_PLATFORM:-}" ] && platform_flags=(--platform "$HYDRA_PLATFORM")

    "$DOCKER" run --rm -i \
        ${tty_flags[@]+"${tty_flags[@]}"} \
        ${platform_flags[@]+"${platform_flags[@]}"} \
        --user "$(id -u):$(id -g)" \
        -v "$dir:/project" \
        -v "$ROOT:/hydra/Hydra" \
        -w /project \
        -e HOME=/tmp \
        -e HYDRA_LIB_DIR=/hydra \
        -e PLATFORMIO_BUILD_CACHE_DIR=/tmp/pio-cache \
        -e TERM="${TERM:-dumb}" \
        "$IMAGE" "$@"
}

cmd_shell()  { require_daemon; docker_run bash; }
cmd_run()    { require_daemon; docker_run "$@"; }

cmd_clean() {
    # Usuwa wyłącznie artefakty budowy. Toolchainy siedzą w obrazie, więc nie
    # ma tu czego stracić — i nie trzeba do tego roota.
    info "Usuwam artefakty budowy"
    rm -rf "$ROOT"/.pio "$ROOT"/test/build "$ROOT"/test/build-asan "$ROOT"/test/build-tsan
}

case "${1:-help}" in
    build) shift; cmd_build "$@" ;;
    pull)  shift; cmd_pull  "$@" ;;
    test)  shift; cmd_test  "$@" ;;
    fw)    shift; cmd_fw    "$@" ;;
    ci)    shift; cmd_ci    "$@" ;;
    project) shift; cmd_project "$@" ;;
    shell) shift; cmd_shell "$@" ;;
    run)   shift; cmd_run   "$@" ;;
    clean) shift; cmd_clean "$@" ;;
    help|-h|--help)
        sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        ;;
    *) die "Nieznane polecenie: $1 (spróbuj: help)" ;;
esac
