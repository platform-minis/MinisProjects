#pragma once
/**
 * Hydra — callback bez alokacji (rozdz. 3: sygnały/sloty w stylu Qt, bez MOC).
 *
 * Delegate<Sig> zachowuje się jak std::function, ale przechowuje domknięcie
 * w buforze inline o stałym rozmiarze — zgodnie z zasadą "brak alokacji po
 * App::begin()" (rozdz. 11). Lambda przekraczająca budżet nie skompiluje się,
 * zamiast po cichu sięgnąć na stertę.
 */

#include <stddef.h>

#include <new>
#include <type_traits>
#include <utility>

#include "hydra/core/Expected.hpp"
#include "hydra/core/Types.hpp"

namespace hydra {

/**
 * Domyślny budżet na przechwycone zmienne: 4 wskaźniki.
 *
 * Wielkość wzięta z realnego kodu sterowników: transakcja na magistrali
 * przechwytuje zwykle `this` i dwa–trzy bufory wynikowe. Ciaśniejszy budżet
 * zmuszałby do opakowywania argumentów w struktury pomocnicze bez żadnego
 * zysku, a szerszy niepotrzebnie powiększałby tablicę subskrypcji EventBusa.
 */
#ifndef HYDRA_DELEGATE_CAPACITY
#  define HYDRA_DELEGATE_CAPACITY (4 * sizeof(void*))
#endif

template <typename Sig, size_t Cap = HYDRA_DELEGATE_CAPACITY>
class Delegate;

template <typename R, typename... Args, size_t Cap>
class Delegate<R(Args...), Cap> {
public:
    Delegate() = default;

    template <typename F, typename = typename std::enable_if<
                  !std::is_same<typename std::decay<F>::type, Delegate>::value>::type>
    Delegate(F&& f) {
        using Fn = typename std::decay<F>::type;
        static_assert(sizeof(Fn) <= Cap,
                      "Domknięcie za duże dla Delegate — przechwyć wskaźnik zamiast kopii "
                      "albo zwiększ HYDRA_DELEGATE_CAPACITY");
        static_assert(alignof(Fn) <= alignof(max_align_t), "Zbyt duże wyrównanie domknięcia");
        new (&storage_) Fn(std::forward<F>(f));
        invoke_ = [](const void* s, Args... a) -> R {
            return (*const_cast<Fn*>(static_cast<const Fn*>(s)))(std::forward<Args>(a)...);
        };
        destroy_ = std::is_trivially_destructible<Fn>::value
                       ? nullptr
                       : +[](void* s) { static_cast<Fn*>(s)->~Fn(); };
        copy_ = +[](void* dst, const void* src) { new (dst) Fn(*static_cast<const Fn*>(src)); };
    }

    Delegate(const Delegate& o) { copyFrom(o); }
    Delegate& operator=(const Delegate& o) {
        if (this != &o) { reset(); copyFrom(o); }
        return *this;
    }
    ~Delegate() { reset(); }

    explicit operator bool() const { return invoke_ != nullptr; }

    R operator()(Args... args) const {
        return invoke_(&storage_, std::forward<Args>(args)...);
    }

    void reset() {
        if (destroy_) destroy_(&storage_);
        invoke_ = nullptr; destroy_ = nullptr; copy_ = nullptr;
    }

private:
    void copyFrom(const Delegate& o) {
        if (o.copy_) o.copy_(&storage_, &o.storage_);
        invoke_ = o.invoke_; destroy_ = o.destroy_; copy_ = o.copy_;
    }

    using Storage = typename std::aligned_storage<Cap, alignof(max_align_t)>::type;
    mutable Storage storage_{};
    R    (*invoke_)(const void*, Args...) = nullptr;
    void (*destroy_)(void*)               = nullptr;
    void (*copy_)(void*, const void*)     = nullptr;
};

/**
 * Sygnał w stylu Qt: lista slotów o stałej pojemności.
 * Emisja woła sloty w kolejności podłączenia, w kontekście emitującego taska.
 */
template <size_t MaxSlots, typename... Args>
class Signal : NonCopyable {
public:
    using Slot = Delegate<void(Args...)>;

    /** Podłącza slot. Zwraca uchwyt do rozłączenia albo Err::OutOfMemory. */
    Result<u8> connect(Slot s) {
        if (count_ >= MaxSlots) return unexpected(Err::OutOfMemory);
        slots_[count_] = s;
        return static_cast<u8>(count_++);
    }

    void disconnect(u8 handle) {
        if (handle >= count_) return;
        for (size_t i = handle; i + 1 < count_; ++i) slots_[i] = slots_[i + 1];
        slots_[--count_].reset();
    }

    void emit(Args... args) const {
        for (size_t i = 0; i < count_; ++i) {
            if (slots_[i]) slots_[i](args...);
        }
    }

    size_t slotCount() const { return count_; }

private:
    Slot   slots_[MaxSlots];
    size_t count_ = 0;
};

}  // namespace hydra
