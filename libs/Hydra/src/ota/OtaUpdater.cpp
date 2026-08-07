/** Hydra — implementacja aktualizacji przez sieć (rozdz. 7.2). */

#include "hydra/ota/OtaUpdater.hpp"

#if HYDRA_ENABLE_OTA && HYDRA_ENABLE_NET

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hydra/core/Events.hpp"
#include "hydra/core/Log.hpp"

HYDRA_LOG_MODULE("ota")

namespace hydra {
namespace ota {

Status OtaUpdater::begin(net::IClient& client, IFirmwareStore& store, Millis now) {
    client_ = &client;
    store_  = &store;

    // Obraz w trybie próbnym: startujemy z niego po raz pierwszy i mamy
    // ograniczony czas na potwierdzenie, że działa.
    if (store.pendingVerify()) {
        transition(OtaState::PendingConfirm);
        confirmDeadline_ = now;  // ustawiany na właściwą wartość w start/step
        HYDRA_LOGW("obraz w trybie próbnym — wymagane potwierdzenie sprawności");
    } else {
        transition(OtaState::Idle);
    }
    return ok();
}

void OtaUpdater::transition(OtaState next) {
    if (next == state_) return;
    state_ = next;
    EventBus::publish(OtaProgress{received_, total_, percent(), static_cast<u8>(next)});
}

u8 OtaUpdater::percent() const {
    if (total_ == 0) return 0;
    const u32 value = received_ * 100u / total_;
    return static_cast<u8>(value > 100 ? 100 : value);
}

Status OtaUpdater::start(const Config& cfg, Millis now) {
    if (!client_ || !store_) return fail(Err::NotInitialized);
    if (!cfg.host || !cfg.path) return fail(Err::BadArgument);
    if (state_ == OtaState::Downloading || state_ == OtaState::Headers ||
        state_ == OtaState::Connecting) {
        return fail(Err::Busy);
    }
    // Aktualizacja w trakcie trybu próbnego zamurowałaby urządzenie:
    // straciłoby ono jedyny znany sprawny obraz.
    if (state_ == OtaState::PendingConfirm) return fail(Err::Busy);

    cfg_          = cfg;
    received_     = 0;
    total_        = 0;
    error_        = Err::None;
    headerLength_ = 0;
    lastData_     = now;
    useHmac_      = !cfg.hmacKey.empty();

    sha_.reset();
    if (useHmac_) {
        hmac_.begin(CByteSpan{reinterpret_cast<const u8*>(cfg.hmacKey.reveal()),
                              cfg.hmacKey.length()});
    }

    ++stats_.attempts;
    transition(OtaState::Connecting);

    if (auto r = client_->connect(cfg.host, cfg.port, cfg.connectTimeoutMs); !r) {
        finishWithError(r.error());
        return r;
    }
    if (auto r = sendRequest(); !r) {
        finishWithError(r.error());
        return r;
    }

    transition(OtaState::Headers);
    HYDRA_LOGI("pobieranie %s:%u%s", cfg.host, static_cast<unsigned>(cfg.port), cfg.path);
    return ok();
}

Status OtaUpdater::sendRequest() {
    char request[256];
    const int n = snprintf(request, sizeof(request),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "User-Agent: Hydra\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           cfg_.path, cfg_.host);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(request)) return fail(Err::OutOfRange);

    const size_t sent = client_->write(
        CByteSpan{reinterpret_cast<const u8*>(request), static_cast<size_t>(n)});
    return sent == static_cast<size_t>(n) ? ok() : fail(Err::IoError);
}

void OtaUpdater::finishWithError(Err error) {
    error_ = error;
    if (store_) store_->abort();
    if (client_) client_->stop();

    ++stats_.failures;
    transition(OtaState::Failed);
    EventBus::publish(OtaFinished{false, error, received_});
    HYDRA_LOGE("aktualizacja nieudana: %s", toString(error));
}

void OtaUpdater::abort() {
    if (state_ == OtaState::Idle || state_ == OtaState::Ready) return;
    finishWithError(Err::None);
}

void OtaUpdater::consumeHeaders(Millis now) {
    u8 byte = 0;
    while (client_->available() > 0) {
        if (client_->read(ByteSpan{&byte, 1}) != 1) break;
        lastData_ = now;

        if (headerLength_ + 1 >= sizeof(headerBuffer_)) {
            finishWithError(Err::OutOfRange);
            return;
        }
        headerBuffer_[headerLength_++] = static_cast<char>(byte);
        headerBuffer_[headerLength_]   = '\0';

        // Pusty wiersz kończy nagłówki; od tego miejsca płynie sam obraz.
        if (headerLength_ >= 4 &&
            memcmp(headerBuffer_ + headerLength_ - 4, "\r\n\r\n", 4) == 0) {
            break;
        }
        continue;
    }

    if (headerLength_ < 4 ||
        memcmp(headerBuffer_ + headerLength_ - 4, "\r\n\r\n", 4) != 0) {
        return;  // nagłówki jeszcze niekompletne
    }

    // Kod odpowiedzi. Serwer, który zwraca 404 albo 500, przysyła w ciele
    // stronę błędu — bez tego sprawdzenia trafiłaby ona do pamięci programu.
    int status = 0;
    if (sscanf(headerBuffer_, "HTTP/%*d.%*d %d", &status) != 1 || status != 200) {
        HYDRA_LOGE("serwer odpowiedział kodem %d", status);
        finishWithError(Err::Protocol);
        return;
    }

    const char* lengthHeader = strstr(headerBuffer_, "Content-Length:");
    if (!lengthHeader) lengthHeader = strstr(headerBuffer_, "content-length:");
    if (!lengthHeader) {
        // Bez znanego rozmiaru nie da się sprawdzić, czy obraz się zmieści,
        // ani stwierdzić, że pobrano całość.
        finishWithError(Err::Protocol);
        return;
    }
    total_ = static_cast<u32>(strtoul(lengthHeader + 15, nullptr, 10));
    if (total_ == 0) {
        finishWithError(Err::Protocol);
        return;
    }

    if (auto r = store_->begin(total_); !r) {
        HYDRA_LOGE("obraz %lu B nie mieści się w %lu B",
                   static_cast<unsigned long>(total_),
                   static_cast<unsigned long>(store_->capacity()));
        finishWithError(r.error());
        return;
    }

    transition(OtaState::Downloading);
}

void OtaUpdater::consumeBody(Millis now) {
    u8 chunk[HYDRA_OTA_CHUNK];

    // Ograniczona porcja pracy na wywołanie: task sieciowy ma jeszcze inne
    // obowiązki, a pobranie kilkuset kilobajtów nie może go zablokować.
    for (u8 pass = 0; pass < 4; ++pass) {
        if (client_->available() == 0) break;

        size_t want = sizeof(chunk);
        const u32 remaining = total_ - received_;
        if (remaining < want) want = remaining;
        if (want == 0) break;

        const size_t read = client_->read(ByteSpan{chunk, want});
        if (read == 0) break;

        lastData_ = now;
        const CByteSpan data{chunk, read};

        sha_.update(data);
        if (useHmac_) hmac_.update(data);

        if (auto r = store_->write(data); !r) {
            finishWithError(r.error());
            return;
        }
        received_ += static_cast<u32>(read);
    }

    if (received_ >= total_) {
        transition(OtaState::Verifying);
        verify();
        return;
    }

    if (now - lastData_ >= cfg_.stallTimeoutMs) {
        HYDRA_LOGE("pobieranie zatrzymało się na %lu z %lu B",
                   static_cast<unsigned long>(received_), static_cast<unsigned long>(total_));
        finishWithError(Err::Timeout);
        return;
    }

    if (!client_->connected() && client_->available() == 0) {
        // Zerwane połączenie przed końcem obrazu — to nie jest kompletna
        // aktualizacja, choćby brakowało jednego bajtu.
        finishWithError(Err::IoError);
    }
}

void OtaUpdater::verify() {
    u8 digest[util::kSha256Size];
    sha_.finish(digest);

    if (cfg_.expectedSha256) {
        u8 expected[util::kSha256Size];
        if (!util::Sha256::fromHex(cfg_.expectedSha256, expected)) {
            finishWithError(Err::BadArgument);
            return;
        }
        if (!util::Sha256::equal(digest, expected)) {
            char actual[util::kSha256Size * 2 + 1];
            util::Sha256::toHex(digest, actual, sizeof(actual));
            HYDRA_LOGE("skrót obrazu się nie zgadza: %s", actual);
            finishWithError(Err::Protocol);
            return;
        }
    }

    if (useHmac_) {
        u8 mac[util::kSha256Size];
        hmac_.finish(mac);
        // Bez oczekiwanej wartości HMAC nie ma czego porównać — konfiguracja
        // zapowiadała potwierdzenie autentyczności, którego nie da się wykonać.
        if (!cfg_.expectedSha256) {
            finishWithError(Err::BadArgument);
            return;
        }
    }

    if (auto r = store_->finish(); !r) {
        finishWithError(r.error());
        return;
    }
    if (auto r = store_->commit(); !r) {
        finishWithError(r.error());
        return;
    }

    client_->stop();
    ++stats_.successes;
    transition(OtaState::Ready);
    EventBus::publish(OtaFinished{true, Err::None, received_});
    HYDRA_LOGI("obraz %lu B zapisany i zweryfikowany — wymagany restart",
               static_cast<unsigned long>(received_));
}

void OtaUpdater::step(Millis now) {
    if (!client_ || !store_) return;

    switch (state_) {
        case OtaState::Headers:
            consumeHeaders(now);
            break;

        case OtaState::Downloading:
            consumeBody(now);
            break;

        case OtaState::PendingConfirm: {
            if (confirmDeadline_ == 0) confirmDeadline_ = now + cfg_.confirmTimeoutMs;
            if (static_cast<i32>(now - confirmDeadline_) < 0) break;

            // Obraz nie potwierdził sprawności w wyznaczonym czasie. Powrót
            // do poprzedniego jest jedyną drogą, która nie kończy się wizytą
            // z programatorem.
            HYDRA_LOGE("brak potwierdzenia sprawności — powrót do poprzedniego obrazu");
            rollback();
            break;
        }

        default:
            break;
    }
}

Status OtaUpdater::confirmRunningImage() {
    if (!store_) return fail(Err::NotInitialized);
    if (state_ != OtaState::PendingConfirm) return ok();

    HYDRA_CHECK(store_->markValid());
    confirmDeadline_ = 0;
    transition(OtaState::Idle);
    HYDRA_LOGI("obraz potwierdzony jako sprawny");
    return ok();
}

Status OtaUpdater::rollback() {
    if (!store_) return fail(Err::NotInitialized);

    HYDRA_CHECK(store_->rollback());
    ++stats_.rollbacks;
    confirmDeadline_ = 0;
    transition(OtaState::Idle);
    EventBus::publish(RebootRequest{nameId("ota"), 1});
    return ok();
}

}  // namespace ota
}  // namespace hydra

#endif  // HYDRA_ENABLE_OTA && HYDRA_ENABLE_NET
