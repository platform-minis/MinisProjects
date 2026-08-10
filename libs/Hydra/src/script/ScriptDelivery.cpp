/**
 * Hydra — dostarczanie skryptu przez sieć.
 *
 * Cała nietrywialna logika jest w `opCommit()` i `step()`: pierwsze przełącza
 * obraz i otwiera okres próbny, drugie ten okres zamyka — potwierdzeniem albo
 * wycofaniem. Reszta to rozpakowanie JSON-a i odpowiedzi.
 */

#include "hydra/core/Config.hpp"

#if HYDRA_ENABLE_SCRIPT && HYDRA_ENABLE_MINIS

#include "hydra/script/ScriptDelivery.hpp"

#include <string.h>

#include "hydra/core/App.hpp"
#include "hydra/core/Log.hpp"
#include "hydra/util/Base64.hpp"
#include "hydra/util/Sha256.hpp"

HYDRA_LOG_MODULE("script.ota")

namespace hydra {
namespace script {

Status ScriptDelivery::configure(const Config& cfg) {
    if (state() == ModuleState::Running) return fail(Err::Busy);
    if (cfg.minis == nullptr || cfg.script == nullptr || cfg.store == nullptr) {
        return fail(Err::BadArgument);
    }
    cfg_ = cfg;
    return ok();
}

// ---------------------------------------------------------------------------
// Cykl życia
// ---------------------------------------------------------------------------

Status ScriptDelivery::onInit() {
    if (cfg_.minis == nullptr) return fail(Err::NotInitialized);

    // Obraz wbudowany staje się aktywny bez zajmowania slotu — dzięki temu
    // pierwsza aktualizacja ma dokąd się wycofać, choć nic jeszcze nie przyszło.
    if (cfg_.script != nullptr) {
        // Moduł skryptowy trzyma obraz w konfiguracji; magazyn musi znać ten
        // sam wskaźnik, żeby wycofanie wróciło dokładnie do niego.
        cfg_.store->adoptBuiltin(cfg_.script->image());
    }

    // Obraz z pamięci trwałej, jeśli jakiś przetrwał restart. Musi wejść po
    // przejęciu wbudowanego, żeby wycofanie miało dokąd wrócić także wtedy,
    // gdy odtworzona wersja okaże się zła.
    restorePersisted();

    return cfg_.minis->addExtension(
        kExtType, [this](const char* id, const char* op, json::JsonView params) {
            onRequest(id, op, params);
        });
}

Status ScriptDelivery::onStart() {
    Task::Cfg tcfg{};
    tcfg.name = "script.ota";
    tcfg.prio = cfg_.priority;
    return task_.startPeriodic(tcfg, cfg_.periodMs, [this] { step(App::uptimeMs()); });
}

void ScriptDelivery::onStop() {
    task_.stopAndWait();
    trialActive_ = false;
}

// ---------------------------------------------------------------------------
// Okres próbny
// ---------------------------------------------------------------------------

void ScriptDelivery::step(Millis now) {
    if (!trialActive_) return;

    // Wyłączona `loop()` to jedyny sygnał, jaki moduł daje o skrypcie, który
    // się nie nadaje: seria błędów z rzędu, po której przestaje próbować.
    if (cfg_.script->loopStopped()) {
        rollbackNow("loop() wylaczona w okresie probnym");
        return;
    }

    if (now >= trialUntilMs_) {
        trialActive_ = false;
        cfg_.store->confirm();
        ++stats_.confirms;
        HYDRA_LOGI("nowy skrypt potwierdzony");

        // Zapis dopiero teraz, a nie przy `commit`: do pamięci trwałej trafia
        // wyłącznie wersja, która dowiodła, że wstaje. Inaczej restart w środku
        // okresu próbnego przywracałby właśnie tę, przed którą się bronimy.
        persistActive();
    }
}

void ScriptDelivery::rollbackNow(const char* reason) {
    trialActive_ = false;

    auto restored = cfg_.store->rollback();
    if (!restored) {
        // Nie ma dokąd wrócić. Moduł zostaje z tym, co ma — i tak jest to
        // lepsze niż urządzenie bez skryptu w ogóle.
        HYDRA_LOGE("wycofanie niemozliwe: %s", reason);
        return;
    }

    ++stats_.rollbacks;
    HYDRA_LOGW("wycofanie skryptu: %s", reason);

    // Wersja, do której wracamy, jest sprawdzona — niech przetrwa restart.
    persistActive();

    const auto& image = restored.value();
    auto reloaded = cfg_.script->reload(image.data, image.bytes, "=rollback");
    if (!reloaded) HYDRA_LOGE("poprzedni skrypt tez nie wstal: %s", cfg_.script->engine()->error());
}

// ---------------------------------------------------------------------------
// Protokół
// ---------------------------------------------------------------------------

void ScriptDelivery::onRequest(const char* id, const char* op, json::JsonView params) {
    if (op == nullptr) return respondErr(id, "bad_request", "brak operacji");

    if (strcmp(op, "begin") == 0)  return opBegin(id, params);
    if (strcmp(op, "chunk") == 0)  return opChunk(id, params);
    if (strcmp(op, "commit") == 0) return opCommit(id);
    if (strcmp(op, "abort") == 0)  return opAbort(id);
    if (strcmp(op, "status") == 0) return opStatus(id);

    respondErr(id, "unsupported", "nieznana operacja");
}

void ScriptDelivery::opBegin(const char* id, json::JsonView params) {
    i32 size = 0;
    if (!params.get("size").asInt(size) || size <= 0) {
        return respondErr(id, "bad_request", "brak rozmiaru");
    }

    char hex[2 * util::kSha256Size + 1] = {};
    if (!params.get("sha256").asString(hex, sizeof(hex))) {
        return respondErr(id, "bad_request", "brak skrotu");
    }

    u8 want[util::kSha256Size] = {};
    if (!util::Sha256::fromHex(hex, want)) {
        return respondErr(id, "bad_request", "skrot nie jest szesnastkowy");
    }

    // Wariant obrazu sprawdzamy **przed** transferem, a nie po. Serwer nie wie,
    // co stoi po drugiej stronie, a kod skompilowany z wyprzedzeniem dla obcego
    // celu nie jest gorszy — jest niewykonywalny. Lepiej odmówić teraz niż
    // przesłać kilkadziesiąt kilobajtów i wywalić się na `commit`.
    char variant[24] = {};
    if (!params.get("variant").asString(variant, sizeof(variant))) variant[0] = '\0';

    auto* engine = cfg_.script->engine();
    if (engine == nullptr || !engine->acceptsVariant(variant)) {
        return respondErr(id, "variant", "silnik nie wykona obrazu w tym wariancie");
    }

    // Podpis, jeśli urządzenie ma klucz. Sam skrót mówi wyłącznie, że obraz nie
    // uległ uszkodzeniu w drodze — napastnik policzy go równie dobrze. HMAC
    // wymaga znajomości klucza, więc podmiana wymaga jego zdobycia.
    haveHmac_ = false;
    if (cfg_.hmacKey.length() > 0) {
        char mac[2 * util::kSha256Size + 1] = {};
        if (!params.get("hmac").asString(mac, sizeof(mac)) ||
            !util::Sha256::fromHex(mac, wantHmac_)) {
            ++stats_.unsigned_;
            return respondErr(id, "unsigned", "urzadzenie wymaga podpisanego obrazu");
        }
        haveHmac_ = true;
    }

    // Nazwa jest wygodą diagnostyczną: pojawia się w komunikatach o błędach
    // skryptu, więc „=v7" mówi tam więcej niż „=remote".
    if (!params.get("name").asString(imageName_, sizeof(imageName_))) {
        strcpy(imageName_, "=remote");
    }

    auto started = cfg_.store->beginTransfer(static_cast<size_t>(size), want);
    if (!started) {
        switch (started.error()) {
            case Err::OutOfRange:
                return respondErr(id, "too_large", "obraz nie miesci sie w slocie");
            case Err::Busy:
                return respondErr(id, "busy", "trwa okres probny poprzedniej wersji");
            default:
                return respondErr(id, "failed", "nie mozna rozpoczac transferu");
        }
    }

    ++stats_.begins;
    respondOk(id);
}

void ScriptDelivery::opChunk(const char* id, json::JsonView params) {
    i32 seq = -1;
    if (!params.get("seq").asInt(seq) || seq < 0) {
        return respondErr(id, "bad_request", "brak numeru fragmentu");
    }

    if (!params.get("data").asString(b64_, sizeof(b64_))) {
        return respondErr(id, "too_large", "fragment wiekszy niz bufor");
    }

    auto decoded = util::base64Decode(b64_, strlen(b64_), ByteSpan{chunk_, sizeof(chunk_)});
    if (!decoded) return respondErr(id, "bad_request", "fragment nie jest base64");

    auto appended = cfg_.store->appendChunk(static_cast<u32>(seq),
                                            CByteSpan{chunk_, decoded.value()});
    if (!appended) {
        // Numer poza kolejnością to zgubiona albo powtórzona paczka. Transfer
        // trzeba zacząć od nowa — sklejanie dziurawego obrazu nie ma sensu,
        // a i tak nie przeszłoby weryfikacji skrótu.
        return respondErr(id, "out_of_order", "fragment poza kolejnoscia");
    }

    ++stats_.chunks;
    respondOk(id);
}

void ScriptDelivery::opCommit(const char* id) {
    auto verified = cfg_.store->verifyStaged();
    if (!verified) {
        ++stats_.rejects;
        return respondErr(id, "checksum", "skrot sie nie zgadza");
    }

    // Podpis sprawdzamy po skrócie, a przed przełączeniem: obraz uszkodzony
    // w drodze i obraz podstawiony to dwie różne awarie i zasługują na dwie
    // różne odpowiedzi.
    if (haveHmac_ && !verifySignature()) {
        ++stats_.unsigned_;
        cfg_.store->abortTransfer();
        return respondErr(id, "signature", "podpis sie nie zgadza");
    }

    auto activated = cfg_.store->activateStaged();
    if (!activated) return respondErr(id, "failed", "nie mozna przelaczyc obrazu");

    if (!applyImage(activated.value())) {
        // Skrypt nie wstał — nie ma czego obserwować, wycofujemy od razu.
        rollbackNow("nowy skrypt nie wstal");
        return respondErr(id, "load_failed", "skrypt nie wstal, wycofano");
    }

    trialUntilMs_ = App::uptimeMs() + cfg_.trialMs;
    trialActive_  = true;
    ++stats_.commits;

    HYDRA_LOGI("nowy skrypt wczytany, okres probny %u ms", cfg_.trialMs);
    respondOk(id, "{\"trial\":true}");
}

void ScriptDelivery::opAbort(const char* id) {
    cfg_.store->abortTransfer();
    respondOk(id);
}

void ScriptDelivery::opStatus(const char* id) {
    u8 buf[192];
    json::JsonWriter out{ByteSpan{buf, sizeof(buf)}};

    char hex[2 * util::kSha256Size + 1] = {};
    util::Sha256::toHex(cfg_.store->active().sha, hex, sizeof(hex));

    out.beginObject();
    // Nazwa silnika mówi serwerowi, co ma przysłać: „lua" chce źródła,
    // „wasm3" przenośnego bajtkodu. Bez tego nadawca musiałby zgadywać.
    out.key("engine").value(cfg_.script->engine() != nullptr
                                ? cfg_.script->engine()->name() : "none");
    out.key("receiving").value(cfg_.store->receiving());
    out.key("received").value(static_cast<i32>(cfg_.store->received()));
    out.key("expected").value(static_cast<i32>(cfg_.store->expected()));
    out.key("capacity").value(static_cast<i32>(cfg_.store->capacity()));
    out.key("trial").value(trialActive_);
    out.key("canRollback").value(cfg_.store->canRollback());
    out.key("sha256").value(hex);
    out.endObject();

    respondOk(id, out.ok() ? out.text() : nullptr);
}

// ---------------------------------------------------------------------------
// Pomocnicze
// ---------------------------------------------------------------------------

void ScriptDelivery::persistActive() {
    if (cfg_.persist == nullptr || !cfg_.persist->enabled()) return;

    const auto& active = cfg_.store->active();
    if (!active.valid()) return;

    // Obraz wbudowany nie ma po co lądować w pamięci trwałej — jest w firmware.
    // Zapisany plik trzeba wtedy usunąć, inaczej po restarcie wróciłaby wersja,
    // od której właśnie odeszliśmy.
    if (active.slot < 0) {
        (void)cfg_.persist->clear();
        return;
    }

    auto saved = cfg_.persist->save(active.span());
    if (!saved) HYDRA_LOGW("obraz nie zostal zapisany na trwale");
}

void ScriptDelivery::restorePersisted() {
    if (cfg_.persist == nullptr || !cfg_.persist->enabled()) return;

    // Odczyt idzie prosto do wolnego slotu magazynu — bez drugiego bufora
    // wielkości obrazu, na który na urządzeniu zwykle nie ma miejsca.
    auto slot = cfg_.store->stagingBuffer();
    if (slot.data() == nullptr) return;

    auto loaded = cfg_.persist->load(slot);
    if (!loaded) return;   // brak pliku albo uszkodzony — zostajemy z wbudowanym

    auto adopted = cfg_.store->adoptRestored(loaded.value());
    if (!adopted) {
        HYDRA_LOGE("odtworzony obraz nie zmiescil sie w magazynie");
        return;
    }

    const auto& image = cfg_.store->active();
    auto reloaded = cfg_.script->reload(image.data, image.bytes, "=trwaly");
    if (!reloaded) {
        // Odtworzony obraz nie wstał. Wracamy do wbudowanego i kasujemy plik,
        // żeby nie próbować go przy każdym rozruchu — pętla restartów byłaby
        // gorsza od utraty ostatniej wersji.
        HYDRA_LOGE("odtworzony obraz nie wstal: %s", cfg_.script->engine()->error());
        (void)cfg_.persist->clear();
        auto back = cfg_.store->rollback();
        if (back) (void)cfg_.script->reload(back.value().data, back.value().bytes, "=wbudowany");
        return;
    }

    HYDRA_LOGI("odtworzono obraz z pamieci trwalej, %u B", static_cast<u32>(image.bytes));
}

bool ScriptDelivery::verifySignature() const {
    // Liczymy nad tym samym obrazem, który przed chwilą przeszedł weryfikację
    // skrótu — czyli nad zawartością slotu, a nie nad tym, co zapowiedział
    // nadawca. Inaczej podpis potwierdzałby deklarację, a nie dane.
    const auto& staged = cfg_.store->stagedImage();
    if (staged.data() == nullptr) return false;

    u8 got[util::kSha256Size] = {};
    util::HmacSha256::compute(
        CByteSpan{reinterpret_cast<const u8*>(cfg_.hmacKey.reveal()), cfg_.hmacKey.length()},
        staged, got);

    return util::Sha256::equal(got, wantHmac_);
}

bool ScriptDelivery::applyImage(const ImageRef& image) {
    auto reloaded = cfg_.script->reload(image.data, image.bytes, imageName_);
    if (!reloaded) {
        HYDRA_LOGE("nowy skrypt odrzucony: %s", cfg_.script->engine()->error());
        return false;
    }
    return true;
}

void ScriptDelivery::respondOk(const char* id, const char* dataJson) {
    (void)cfg_.minis->extRespond(kExtType, id, true, dataJson);
}

void ScriptDelivery::respondErr(const char* id, const char* code, const char* message) {
    (void)cfg_.minis->extRespond(kExtType, id, false, nullptr, code, message);
}

}  // namespace script
}  // namespace hydra

#endif  // HYDRA_ENABLE_SCRIPT && HYDRA_ENABLE_MINIS
