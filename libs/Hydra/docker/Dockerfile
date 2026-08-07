# Hydra — obraz do budowania wsadów na wszystkie platformy docelowe.
#
# Zawiera PlatformIO Core (bez IDE), komplet platform z platformio.ini wraz
# z pobranymi toolchainami, oraz kompilator hostowy, żeby ten sam obraz
# uruchamiał testy jednostkowe. Dzięki temu CI i stanowisko programisty
# budują tym samym.
#
# Wnioski z obrazu do pico (src/RPiPico2DisplayDVI/build.sh), który powstał
# wcześniej w tym repozytorium:
#
#  1. Tamten skrypt kończy się `chown -R`, a `clean-all` wymaga `sudo rm -rf`.
#     Oba wynikają z tego samego: kontener pisał jako root do podmontowanego
#     katalogu. Tutaj obraz jest przygotowany pod uruchomienie z `--user`,
#     więc pliki od razu należą do wywołującego i nie ma czego naprawiać.
#  2. Tamten obraz nazywa się `mycastle-pico:local`, ale jego przepisu nie ma
#     w repozytorium — kto straci obraz, traci możliwość zbudowania projektu.
#     Ten plik jest wersjonowany razem z kodem.
#  3. Zależności pobierane w trakcie budowy trafiały do wolumenu, żeby nie
#     ściągać ich przy każdym uruchomieniu. Tutaj idą o krok dalej: toolchainy
#     są wpieczone w obraz, więc sama budowa wsadu nie rusza sieci.
#
# Z obrazu mycastle-arduino:local przejęte wprost: katalog danych przeniesiony
# pod /opt i otwarty dla dowolnego UID (tam `/opt/arduino15`, tutaj `/opt/pio`),
# katalog roboczy /workspace oraz przypinanie wersji zamiast „co akurat jest".

ARG PYTHON_TAG=3.12-slim-bookworm
FROM python:${PYTHON_TAG}

# Wersja Core podawana jawnie — inaczej ten sam Dockerfile daje za pół roku
# inny obraz. Wersje platform pinuje sam obraz: raz pobrane zostają w środku
# i PlatformIO nie sięgnie po nowsze, dopóki platformio.ini ich nie wymusza.
ARG PIO_VERSION=6.1.16

# Które środowiska wpiec. Domyślnie wszystkie z platformio.ini; przy pracy
# nad jedną platformą warto zawęzić — pełny obraz waży kilka gigabajtów.
ARG PIO_ENVS="esp32s3 esp32c3 pico pico2 stm32g4"

# Wersje platform przypięte tak samo, jak w obrazie mycastle-arduino przypięte
# są rdzenie (esp32@3.0.7, esp8266@3.1.2). Bez tego obraz zbudowany za miesiąc
# zawiera inny kompilator niż ten, na którym wsad był sprawdzony.
ARG PLATFORM_ESPRESSIF32=7.0.1
ARG PLATFORM_STSTM32=19.7.1
ARG PLATFORM_RASPBERRYPI=9c167c6b8aac4f4cfa6d55a0c4e5b848795150c0

ENV PLATFORMIO_CORE_DIR=/opt/pio \
    PLATFORMIO_SETTING_ENABLE_TELEMETRY=No \
    PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL=0 \
    PLATFORMIO_SETTING_CHECK_PLATFORMS_INTERVAL=0 \
    PLATFORMIO_SETTING_CHECK_LIBRARIES_INTERVAL=0 \
    PLATFORMIO_DISABLE_PROGRESSBAR=true \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

# git — platforma raspberrypi ciągnięta jest wprost z repozytorium.
# build-essential i make — testy hostowe w tym samym obrazie.
# cppcheck — narzędzie wskazane w platformio.ini (check_tool).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cppcheck \
        git \
        make \
        python3-serial \
        python3-venv \
        srecord \
        udev \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN pip install "platformio==${PIO_VERSION}"

# Prosty szkic budowany dla każdego środowiska. To jedyny pewny sposób, żeby
# ściągnąć dokładnie te toolchainy, których potem użyje prawdziwy build:
# `pio pkg install` pobiera pakiety wymagane przez platformę, a nie te, które
# dobiera konkretna płytka. Jeśli którejś platformy nie da się zbudować na tej
# architekturze, dowiadujemy się tutaj, a nie przy pierwszym wsadzie.
COPY warmup /warmup
RUN cd /warmup && \
    sed -i "s|@PLATFORM_ESPRESSIF32@|${PLATFORM_ESPRESSIF32}|g; \
            s|@PLATFORM_STSTM32@|${PLATFORM_STSTM32}|g; \
            s|@PLATFORM_RASPBERRYPI@|${PLATFORM_RASPBERRYPI}|g" platformio.ini && \
    for env in ${PIO_ENVS}; do \
        echo "=== rozgrzewka: $env ===" && \
        pio run -e "$env" || exit 1; \
    done && \
    rm -rf /warmup/.pio

# Ślad tego, co faktycznie wylądowało w obrazie. Przy zgłoszeniu „u mnie się
# buduje, a w CI nie" to pierwsza rzecz, którą warto porównać.
RUN { echo "# Hydra — zawartość obrazu"; \
      echo; \
      echo "PlatformIO Core: $(pio --version)"; \
      echo "Python:          $(python --version)"; \
      echo "Architektura:    $(uname -m)"; \
      echo "Środowiska:      ${PIO_ENVS}"; \
      echo; \
      pio pkg list --global 2>/dev/null || true; \
    } > /opt/pio/hydra-image.txt

# Uruchomienie z --user oznacza dowolny UID, także taki, którego nie ma
# w /etc/passwd. Katalog Core i katalog domowy muszą być zapisywalne dla
# każdego, inaczej PlatformIO wywraca się przy pierwszej próbie zapisu cache.
ENV HOME=/home/hydra
RUN mkdir -p /home/hydra /workspace && \
    chmod -R a+rwX /opt/pio /home/hydra /workspace

WORKDIR /workspace

# Bez tego `docker run` bez polecenia kończy się natychmiast, zamiast dać
# powłokę, w której da się cokolwiek sprawdzić.
CMD ["bash"]
