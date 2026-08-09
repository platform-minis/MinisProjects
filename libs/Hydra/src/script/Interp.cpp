/**
 * Hydra — most między Lua a frameworkiem.
 *
 * Cztery rzeczy dzieją się tutaj i nigdzie indziej:
 *   - podstawienie statycznej puli pod `lua_Alloc`,
 *   - trampolina zamieniająca `lua_CFunction` na `NativeFn` z `Ctx`,
 *   - przechwycenie błędów tak, żeby żaden `longjmp` nie przeszedł przez
 *     ramkę C++ z destruktorem,
 *   - wywłaszczanie skryptu przez pułapkę licznikową (`Job`).
 */

#include "hydra/script/Script.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hydra/core/Log.hpp"

#include "LuaInternal.hpp"

HYDRA_LOG_MODULE("lua")

namespace hydra {
namespace script {

// ---------------------------------------------------------------------------
// Dostęp warstwy wewnętrznej do prywatnych pól
// ---------------------------------------------------------------------------

namespace detail {
struct CtxAccess {
    static Ctx make(lua_State* L, Interp* interp, void* user) {
        Ctx c;
        c.state_  = L;
        c.interp_ = interp;
        c.user_   = user;
        c.failed_ = false;
        return c;
    }
    static bool failed(const Ctx& c) { return c.failed_; }

    static void chargeBudget(Job& job) { job.steps_ += job.budget_; }
};
}  // namespace detail

namespace {

/**
 * Domyślna pula sterty skryptu.
 *
 * Jedna na obraz programu — bo w praktycznie każdym zastosowaniu interpreter
 * jest jeden. Drugi `Interp` bez własnej puli dostanie Err::Busy zamiast po
 * cichu współdzielić pamięć z pierwszym.
 */
alignas(8) u8 gDefaultPool[HYDRA_SCRIPT_HEAP_BYTES];
bool gDefaultPoolTaken = false;

/** Nazwa pola z tabelą bibliotek — potrzebna przy rejestracji zagnieżdżonej. */
constexpr size_t kMaxLibNameLen = 48;

}  // namespace

// ---------------------------------------------------------------------------
// Punkty wejścia wołane przez Lua (linkowanie C)
// ---------------------------------------------------------------------------

extern "C" {

/**
 * Przejściówka na statyczną pulę.
 *
 * Kontrakt Lua: przy `ptr == nullptr` pole `osize` nie jest rozmiarem, tylko
 * znacznikiem typu tworzonego obiektu — dlatego wolno go użyć wyłącznie
 * w gałęzi zmiany rozmiaru.
 */
static void* hydraLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* heap = static_cast<hydra::script::Heap*>(ud);
    if (nsize == 0) {
        heap->release(ptr);
        return nullptr;
    }
    if (ptr == nullptr) return heap->allocate(nsize);
    return heap->reallocate(ptr, osize, nsize);
}

/**
 * Ostatnia deska ratunku: błąd poza `pcall`. W Hydrze nie powinien wystąpić,
 * bo każde wejście do interpretera jest chronione — jeśli wystąpi, to jest to
 * błąd w warstwie bindingów i ma zostawić ślad.
 */
static int hydraLuaPanic(lua_State* L) {
    const char* msg = lua_tostring(L, -1);
    HYDRA_LOGE("panika interpretera: %s", msg ? msg : "(brak opisu)");
    return 0;  // Lua przerywa program — na urządzeniu oznacza to reset
}

/**
 * Trampolina funkcji natywnej.
 *
 * Domknięcie niesie trzy wartości: wskaźnik na `NativeFn` w pełnym userdata
 * (żeby nie rzutować wskaźnika na funkcję do `void*`), wskaźnik użytkownika
 * i wskaźnik na interpreter.
 *
 * Zgłoszenie błędu jest odłożone: funkcja użytkownika wraca normalnie, a
 * `lua_error` — czyli `longjmp` — wykonuje się dopiero tutaj, gdy ramka C++
 * wywołanej funkcji już nie istnieje.
 */
static int hydraLuaTrampoline(lua_State* L) {
    using namespace hydra::script;

    auto* slot = static_cast<NativeFn*>(lua_touserdata(L, lua_upvalueindex(1)));
    void* user = lua_touserdata(L, lua_upvalueindex(2));
    auto* interp = static_cast<Interp*>(lua_touserdata(L, lua_upvalueindex(3)));
    if (slot == nullptr || *slot == nullptr) {
        return luaL_error(L, "funkcja natywna nie jest podpieta");
    }

    Ctx ctx = detail::CtxAccess::make(L, interp, user);
    const int results = (*slot)(ctx);

    if (detail::CtxAccess::failed(ctx)) return lua_error(L);
    return results < 0 ? 0 : results;
}

/**
 * Pułapka licznikowa wywłaszczająca skrypt po wyczerpaniu budżetu.
 *
 * Manual Lua 5.4 dopuszcza `lua_yield` wyłącznie z pułapki licznikowej albo
 * wierszowej i wyłącznie bez wartości — stąd taka, a nie inna konstrukcja.
 * Wywołanie nie wraca.
 */
static void hydraLuaBudgetHook(lua_State* L, lua_Debug* ar) {
    (void)ar;
    using namespace hydra::script;
    auto* job = *static_cast<Job**>(lua_getextraspace(L));
    if (job != nullptr) detail::CtxAccess::chargeBudget(*job);
    lua_yield(L, 0);
}

}  // extern "C"

// ---------------------------------------------------------------------------
// Ctx — argumenty
// ---------------------------------------------------------------------------

namespace {
inline lua_State* S(void* p) { return static_cast<lua_State*>(p); }
}  // namespace

int  Ctx::argCount() const { return lua_gettop(S(state_)); }
bool Ctx::isNil(int i) const { return lua_isnoneornil(S(state_), i); }
bool Ctx::isNumber(int i) const { return lua_type(S(state_), i) == LUA_TNUMBER; }
bool Ctx::isString(int i) const { return lua_type(S(state_), i) == LUA_TSTRING; }
bool Ctx::isBool(int i) const { return lua_type(S(state_), i) == LUA_TBOOLEAN; }
bool Ctx::isTable(int i) const { return lua_type(S(state_), i) == LUA_TTABLE; }
bool Ctx::isFunction(int i) const { return lua_type(S(state_), i) == LUA_TFUNCTION; }

Result<i32> Ctx::argInt(int index) const {
    lua_State* L  = S(state_);
    int        ok = 0;
    const lua_Integer v = lua_tointegerx(L, index, &ok);
    if (ok) return static_cast<i32>(v);

    // Liczba z częścią ułamkową nie jest błędem — obcinamy ją tak, jak zrobiłby
    // to `math.tointeger` po `math.floor`. Skrypt sterujący liczy w liczbach
    // zmiennoprzecinkowych częściej, niż mu się wydaje.
    const lua_Number n = lua_tonumberx(L, index, &ok);
    if (ok) return static_cast<i32>(n);
    return unexpected(Err::BadArgument);
}

Result<float> Ctx::argNumber(int index) const {
    int              ok = 0;
    const lua_Number n  = lua_tonumberx(S(state_), index, &ok);
    if (!ok) return unexpected(Err::BadArgument);
    return static_cast<float>(n);
}

Result<const char*> Ctx::argStr(int index) const {
    lua_State* L = S(state_);
    // Świadomie bez automatycznej konwersji liczby na napis: `lua_tostring`
    // podmieniłby wartość na stosie, co psuje iterację po tabeli u wołającego.
    if (lua_type(L, index) != LUA_TSTRING) return unexpected(Err::BadArgument);
    const char* s = lua_tostring(L, index);
    if (s == nullptr) return unexpected(Err::BadArgument);
    return s;
}

Result<bool> Ctx::argBool(int index) const {
    lua_State* L = S(state_);
    if (lua_type(L, index) != LUA_TBOOLEAN) return unexpected(Err::BadArgument);
    return lua_toboolean(L, index) != 0;
}

const char* Ctx::text(int index) const {
    // luaL_tolstring odkłada wynik na stos i tam go zostawia. To jest w porządku:
    // stos funkcji natywnej znika po jej powrocie, a do tego czasu napis jest
    // zakotwiczony, więc nie zabierze go odśmiecacz.
    return luaL_tolstring(S(state_), index, nullptr);
}

i32 Ctx::optInt(int index, i32 fallback) const {
    if (isNil(index)) return fallback;
    return argInt(index).value_or(fallback);
}
float Ctx::optNumber(int index, float fallback) const {
    if (isNil(index)) return fallback;
    return argNumber(index).value_or(fallback);
}
const char* Ctx::optStr(int index, const char* fallback) const {
    if (isNil(index)) return fallback;
    return argStr(index).value_or(fallback);
}
bool Ctx::optBool(int index, bool fallback) const {
    if (isNil(index)) return fallback;
    return argBool(index).value_or(fallback);
}

// ---------------------------------------------------------------------------
// Ctx — wyniki
// ---------------------------------------------------------------------------

void Ctx::pushNil() { lua_pushnil(S(state_)); }
void Ctx::pushInt(i32 v) { lua_pushinteger(S(state_), static_cast<lua_Integer>(v)); }
void Ctx::pushNumber(float v) { lua_pushnumber(S(state_), static_cast<lua_Number>(v)); }
void Ctx::pushBool(bool v) { lua_pushboolean(S(state_), v ? 1 : 0); }
void Ctx::pushStr(const char* t) { lua_pushstring(S(state_), t ? t : ""); }
void Ctx::pushStr(const char* t, size_t len) { lua_pushlstring(S(state_), t ? t : "", t ? len : 0); }
void Ctx::pushTable() { lua_newtable(S(state_)); }

void Ctx::setField(const char* key) { lua_setfield(S(state_), -2, key); }
void Ctx::setIndex(i32 index) { lua_seti(S(state_), -2, static_cast<lua_Integer>(index)); }

Result<i32> Ctx::fieldInt(int tableIndex, const char* key) const {
    lua_State* L = S(state_);
    if (lua_type(L, tableIndex) != LUA_TTABLE) return unexpected(Err::BadArgument);
    lua_getfield(L, tableIndex, key);
    int               ok = 0;
    const lua_Integer v  = lua_tointegerx(L, -1, &ok);
    lua_pop(L, 1);
    if (!ok) return unexpected(Err::NotFound);
    return static_cast<i32>(v);
}

Result<float> Ctx::fieldNumber(int tableIndex, const char* key) const {
    lua_State* L = S(state_);
    if (lua_type(L, tableIndex) != LUA_TTABLE) return unexpected(Err::BadArgument);
    lua_getfield(L, tableIndex, key);
    int              ok = 0;
    const lua_Number v  = lua_tonumberx(L, -1, &ok);
    lua_pop(L, 1);
    if (!ok) return unexpected(Err::NotFound);
    return static_cast<float>(v);
}

Result<const char*> Ctx::fieldStr(int tableIndex, const char* key) const {
    lua_State* L = S(state_);
    if (lua_type(L, tableIndex) != LUA_TTABLE) return unexpected(Err::BadArgument);
    lua_getfield(L, tableIndex, key);
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        return unexpected(Err::NotFound);
    }
    // Napis jest zakotwiczony w tabeli, więc przeżyje zdjęcie kopii ze stosu.
    const char* s = lua_tostring(L, -1);
    lua_pop(L, 1);
    return s;
}

Result<i32> Ctx::indexInt(int tableIndex, i32 index) const {
    lua_State* L = S(state_);
    if (lua_type(L, tableIndex) != LUA_TTABLE) return unexpected(Err::BadArgument);
    lua_geti(L, tableIndex, static_cast<lua_Integer>(index));
    int               ok = 0;
    const lua_Integer v  = lua_tointegerx(L, -1, &ok);
    lua_pop(L, 1);
    if (!ok) return unexpected(Err::NotFound);
    return static_cast<i32>(v);
}

Result<u32> Ctx::tableLength(int tableIndex) const {
    lua_State* L = S(state_);
    if (lua_type(L, tableIndex) != LUA_TTABLE) return unexpected(Err::BadArgument);
    return static_cast<u32>(lua_rawlen(L, tableIndex));
}

int Ctx::fail(const char* format, ...) {
    char    text[HYDRA_SCRIPT_ERROR_MAX];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof(text), format, ap);
    va_end(ap);

    lua_pushstring(S(state_), text);
    failed_ = true;
    return -1;
}

// ---------------------------------------------------------------------------
// Interp — cykl życia
// ---------------------------------------------------------------------------

Interp::~Interp() { close(); }

Status Interp::open() { return open(Config{}); }

Status Interp::open(const Config& cfg) {
    if (state_ != nullptr) return fail(Err::AlreadyExists);

    void*  pool  = cfg.pool;
    size_t bytes = cfg.poolBytes;
    bool   usingDefault = false;

    if (pool == nullptr) {
        if (gDefaultPoolTaken) return fail(Err::Busy);
        pool         = gDefaultPool;
        bytes        = sizeof(gDefaultPool);
        usingDefault = true;
    }

    HYDRA_CHECK(heap_.init(pool, bytes));
    if (usingDefault) {
        gDefaultPoolTaken = true;
        ownsDefaultPool_  = true;
    }

    lua_State* L = lua_newstate(hydraLuaAlloc, &heap_);
    if (L == nullptr) {
        if (ownsDefaultPool_) {
            gDefaultPoolTaken = false;
            ownsDefaultPool_  = false;
        }
        setError("pula pamieci za mala na stan interpretera");
        return fail(Err::OutOfMemory);
    }
    lua_atpanic(L, hydraLuaPanic);
    *static_cast<Job**>(lua_getextraspace(L)) = nullptr;
    state_ = L;

    auto libs = openLibs(cfg.libs);
    if (!libs) {
        close();
        return libs;
    }
    return ok();
}

void Interp::close() {
    if (state_ != nullptr) {
        lua_close(S(state_));
        state_ = nullptr;
    }
    flushOutput();
    if (heap_.ready()) heap_.reset();
    // Zwolnienie domyślnej puli dopiero tutaj i tylko przez tego, kto ją zajął:
    // dopóki interpreter żyje, drugi nie ma prawa jej przejąć.
    if (ownsDefaultPool_) {
        gDefaultPoolTaken = false;
        ownsDefaultPool_  = false;
    }
    error_[0] = '\0';
}

Status Interp::openLibs(const Libs& libs) {
    lua_State* L = S(state_);

    struct Entry {
        bool          wanted;
        const char*   name;
        lua_CFunction open;
    };
    const Entry entries[] = {
        {libs.base,      LUA_GNAME,        luaopen_base},
        {libs.table,     LUA_TABLIBNAME,   luaopen_table},
        {libs.string,    LUA_STRLIBNAME,   luaopen_string},
        {libs.math,      LUA_MATHLIBNAME,  luaopen_math},
        {libs.coroutine, LUA_COLIBNAME,    luaopen_coroutine},
        {libs.utf8,      LUA_UTF8LIBNAME,  luaopen_utf8},
        {libs.debug,     LUA_DBLIBNAME,    luaopen_debug},
    };

    for (const Entry& e : entries) {
        if (!e.wanted) continue;
        luaL_requiref(L, e.name, e.open, 1);
        lua_pop(L, 1);
    }

    if (libs.base) {
        // `dofile` i `loadfile` zostają w bibliotece bazowej, ale sięgają po
        // stdio. Na urządzeniu nie ma czego otworzyć, a funkcja, która zawsze
        // zwraca błąd, jest gorsza niż jej brak: sugeruje, że gdzieś jest
        // system plików. Dostęp do danych idzie przez bindingi HAL.
        lua_pushnil(L);
        lua_setglobal(L, "dofile");
        lua_pushnil(L);
        lua_setglobal(L, "loadfile");
    }
    return ok();
}

// ---------------------------------------------------------------------------
// Interp — wykonanie
// ---------------------------------------------------------------------------

void Interp::setError(const char* text) {
    if (text == nullptr) {
        error_[0] = '\0';
        return;
    }
    snprintf(error_, sizeof(error_), "%s", text);
}

void Interp::clearError() { error_[0] = '\0'; }

namespace {
/** Przenosi komunikat ze szczytu stosu Lua do bufora interpretera. */
void captureError(lua_State* L, Interp& interp) {
    const char* msg = lua_tostring(L, -1);
    interp.setError(msg ? msg : "blad bez opisu");
    lua_pop(L, 1);
}
}  // namespace

Status Interp::doString(const char* source, const char* chunkName) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    if (source == nullptr) return fail(Err::BadArgument);
    lua_State* L = S(state_);
    clearError();

    const int loaded = luaL_loadbuffer(L, source, strlen(source), chunkName);
    if (loaded != LUA_OK) {
        captureError(L, *this);
        return fail(mapLuaStatus(loaded));
    }

    const int called = lua_pcall(L, 0, 0, 0);
    flushOutput();
    if (called != LUA_OK) {
        captureError(L, *this);
        return fail(mapLuaStatus(called));
    }
    return ok();
}

bool Interp::hasGlobal(const char* name) const {
    if (state_ == nullptr) return false;
    lua_State* L = S(state_);
    lua_getglobal(L, name);
    const bool present = !lua_isnil(L, -1);
    lua_pop(L, 1);
    return present;
}

bool Interp::hasFunction(const char* name) const {
    if (state_ == nullptr) return false;
    lua_State* L = S(state_);
    lua_getglobal(L, name);
    const bool callable = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return callable;
}

Status Interp::callGlobal(const char* name) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    lua_State* L = S(state_);
    clearError();

    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        setError("brak funkcji o tej nazwie");
        return fail(Err::NotFound);
    }

    const int called = lua_pcall(L, 0, 0, 0);
    flushOutput();
    if (called != LUA_OK) {
        captureError(L, *this);
        return fail(mapLuaStatus(called));
    }
    return ok();
}

u32 Interp::collect() {
    if (state_ == nullptr) return 0;
    lua_gc(S(state_), LUA_GCCOLLECT);
    return heap_.stats().used;
}

// ---------------------------------------------------------------------------
// Interp — zmienne globalne
// ---------------------------------------------------------------------------

Status Interp::setGlobalInt(const char* name, i32 value) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    lua_pushinteger(S(state_), static_cast<lua_Integer>(value));
    lua_setglobal(S(state_), name);
    return ok();
}

Status Interp::setGlobalNumber(const char* name, float value) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    lua_pushnumber(S(state_), static_cast<lua_Number>(value));
    lua_setglobal(S(state_), name);
    return ok();
}

Status Interp::setGlobalStr(const char* name, const char* value) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    lua_pushstring(S(state_), value ? value : "");
    lua_setglobal(S(state_), name);
    return ok();
}

Status Interp::setGlobalBool(const char* name, bool value) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    lua_pushboolean(S(state_), value ? 1 : 0);
    lua_setglobal(S(state_), name);
    return ok();
}

// ---------------------------------------------------------------------------
// Interp — rejestracja funkcji natywnych
// ---------------------------------------------------------------------------

namespace {

/** Odkłada domknięcie trampoliny dla jednej funkcji natywnej. */
void pushNative(lua_State* L, NativeFn fn, void* user, Interp* interp) {
    auto* slot = static_cast<NativeFn*>(lua_newuserdatauv(L, sizeof(NativeFn), 0));
    *slot = fn;
    lua_pushlightuserdata(L, user);
    lua_pushlightuserdata(L, interp);
    lua_pushcclosure(L, hydraLuaTrampoline, 3);
}

}  // namespace

Status Interp::registerFn(const char* name, NativeFn fn, void* user) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    if (name == nullptr || fn == nullptr) return fail(Err::BadArgument);
    pushNative(S(state_), fn, user, this);
    lua_setglobal(S(state_), name);
    return ok();
}

/**
 * Zostawia na szczycie stosu tabelę o podanej nazwie, tworząc brakujące
 * poziomy. Kropka rozdziela poziomy: "hydra.gpio" dołoży pole `gpio` do
 * istniejącej tabeli `hydra` zamiast ją zastąpić.
 */
Status Interp::pushLibTable(const char* name) {
    lua_State* L = S(state_);

    char path[kMaxLibNameLen];
    if (name == nullptr || strlen(name) >= sizeof(path)) return fail(Err::BadArgument);
    snprintf(path, sizeof(path), "%s", name);

    lua_pushglobaltable(L);

    char* segment = path;
    while (segment != nullptr && *segment != '\0') {
        char* dot = strchr(segment, '.');
        if (dot != nullptr) *dot = '\0';

        lua_getfield(L, -1, segment);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);           // to, co tam było (nil albo inna wartość)
            lua_newtable(L);
            lua_pushvalue(L, -1);    // kopia dla rodzica
            lua_setfield(L, -3, segment);
        }
        lua_remove(L, -2);           // rodzic już niepotrzebny

        segment = (dot != nullptr) ? dot + 1 : nullptr;
    }
    return ok();
}

Status Interp::registerLib(const char* tableName, const Reg* regs, void* user) {
    if (state_ == nullptr) return fail(Err::NotInitialized);
    if (regs == nullptr) return fail(Err::BadArgument);
    lua_State* L = S(state_);

    HYDRA_CHECK(pushLibTable(tableName));
    for (const Reg* r = regs; r->name != nullptr; ++r) {
        if (r->fn == nullptr) continue;
        pushNative(L, r->fn, user, this);
        lua_setfield(L, -2, r->name);
    }
    lua_pop(L, 1);
    return ok();
}

// ---------------------------------------------------------------------------
// Job — wykonanie z budżetem instrukcji
// ---------------------------------------------------------------------------

Status Job::start(Interp& interp, const char* globalFn) {
    cancel();
    if (!interp.ready()) return fail(Err::NotInitialized);

    lua_State* L  = S(interp.rawState());
    lua_State* co = lua_newthread(L);
    if (co == nullptr) return fail(Err::OutOfMemory);

    // Kotwica w rejestrze: wątek zdjęty ze stosu bez referencji padłby ofiarą
    // odśmiecacza w środku wykonania.
    ref_ = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_getglobal(co, globalFn);
    if (!lua_isfunction(co, -1)) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref_);
        ref_ = LUA_NOREF;
        interp.setError("brak funkcji o tej nazwie");
        return fail(Err::NotFound);
    }

    *static_cast<Job**>(lua_getextraspace(co)) = this;

    interp_ = &interp;
    thread_ = co;
    state_  = State::Running;
    steps_  = 0;
    budget_ = 0;
    return ok();
}

Job::State Job::resume(u32 budget) {
    if (state_ != State::Running || thread_ == nullptr || interp_ == nullptr) return state_;

    lua_State* co = S(thread_);
    lua_State* L  = S(interp_->rawState());

    budget_ = budget;
    if (budget > 0) {
        lua_sethook(co, hydraLuaBudgetHook, LUA_MASKCOUNT, static_cast<int>(budget));
    } else {
        lua_sethook(co, nullptr, 0, 0);
    }

    int       results = 0;
    const int status  = lua_resume(co, L, 0, &results);
    flushOutput();

    if (status == LUA_YIELD) {
        state_ = State::Running;
        return state_;
    }

    if (status == LUA_OK) {
        state_ = State::Done;
    } else {
        const char* msg = lua_tostring(co, -1);
        interp_->setError(msg ? msg : "blad wykonania");
        state_ = State::Failed;
    }
    release();
    return state_;
}

void Job::cancel() {
    if (state_ == State::Running) state_ = State::Idle;
    release();
}

void Job::release() {
    if (interp_ != nullptr && ref_ != LUA_NOREF && ref_ != 0 && interp_->ready()) {
        luaL_unref(S(interp_->rawState()), LUA_REGISTRYINDEX, ref_);
    }
    if (thread_ != nullptr) {
        *static_cast<Job**>(lua_getextraspace(S(thread_))) = nullptr;
        thread_ = nullptr;
    }
    ref_ = LUA_NOREF;
}

}  // namespace script
}  // namespace hydra
