/**
 * Hydra — backend RTOS na FreeRTOS (ESP32 / RP2040 / RP2350 / STM32).
 *
 * Cała wiedza o różnicach między jądrami żyje w tym pliku (rozdz. 15):
 *   - ESP-IDF trzyma nagłówki w podkatalogu freertos/ i ma własne API pinowania
 *     tasków do rdzeni (xTaskCreatePinnedToCore),
 *   - arduino-pico używa FreeRTOS SMP z vTaskCoreAffinitySet,
 *   - stm32duino ma vanilla FreeRTOS, jednordzeniowy, ze schedulerem startowanym
 *     ręcznie.
 * Rdzeń Hydry nie widzi żadnej z tych różnic.
 */

#include "hydra/core/Config.hpp"

#if !HYDRA_PLAT_HOST

#include <string.h>

#if HYDRA_PLAT_ESP32
#  include "esp_timer.h"
#  include "freertos/FreeRTOS.h"
#  include "freertos/queue.h"
#  include "freertos/semphr.h"
#  include "freertos/task.h"
#else
#  include <FreeRTOS.h>
#  include <queue.h>
#  include <semphr.h>
#  include <task.h>
#  if HYDRA_PLAT_RP2
#    include <pico/time.h>
#  endif
#endif

#if HYDRA_PLAT_RP2
#  include <malloc.h>
#endif

#include "hydra/core/Rtos.hpp"

namespace hydra {
namespace rtos {
namespace {

constexpr u32 kForever = 0xFFFFFFFFu;

/** Mapuje priorytet Hydry na zakres priorytetów FreeRTOS danej platformy. */
UBaseType_t toFreeRtosPrio(Prio p) {
    const UBaseType_t maxPrio = configMAX_PRIORITIES - 1;
    UBaseType_t       v       = tskIDLE_PRIORITY + 1;
    switch (p) {
        case Prio::Idle:     v = tskIDLE_PRIORITY + 1; break;
        case Prio::Low:      v = tskIDLE_PRIORITY + 2; break;
        case Prio::Normal:   v = tskIDLE_PRIORITY + 3; break;
        case Prio::High:     v = tskIDLE_PRIORITY + 4; break;
        case Prio::Realtime: v = maxPrio; break;
    }
    return v > maxPrio ? maxPrio : v;
}

u32 toTimeout(u32 ms) { return ms == kForever ? portMAX_DELAY : pdMS_TO_TICKS(ms); }

}  // namespace

// ---------------------------------------------------------------------------
// Taski
// ---------------------------------------------------------------------------

Result<TaskHandle> spawn(const TaskCfg& cfg, TaskEntry entry, void* arg) {
    if (!entry) return unexpected(Err::BadArgument);

    TaskHandle_t h    = nullptr;
    BaseType_t   rc   = pdFAIL;
    const auto   prio = toFreeRtosPrio(cfg.prio);
    const char*  name = cfg.name ? cfg.name : "task";

#if HYDRA_PLAT_ESP32
    if (cfg.core != Core::Any) {
        rc = xTaskCreatePinnedToCore(entry, name, cfg.stackWords, arg, prio, &h,
                                     static_cast<BaseType_t>(cfg.core));
    } else {
        rc = xTaskCreate(entry, name, cfg.stackWords, arg, prio, &h);
    }
#else
    rc = xTaskCreate(entry, name, cfg.stackWords, arg, prio, &h);
#  if defined(configUSE_CORE_AFFINITY) && (configUSE_CORE_AFFINITY == 1)
    // FreeRTOS SMP na RP2040/RP2350. Pinowanie to optymalizacja, nie mechanizm
    // poprawności (rozdz. 10) — jego brak nie jest błędem.
    if (rc == pdPASS && cfg.core != Core::Any) {
        vTaskCoreAffinitySet(h, 1u << static_cast<u32>(cfg.core));
    }
#  endif
#endif

    if (rc != pdPASS || h == nullptr) return unexpected(Err::OutOfMemory);
    return static_cast<TaskHandle>(h);
}

void kill(TaskHandle h) { vTaskDelete(static_cast<TaskHandle_t>(h)); }

void delayMs(u32 ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }

bool delayUntil(u32& lastWakeTicks, u32 periodMs) {
    TickType_t last   = static_cast<TickType_t>(lastWakeTicks);
    TickType_t period = pdMS_TO_TICKS(periodMs);
    if (period == 0) period = 1;

#if (INCLUDE_xTaskDelayUntil == 1)
    const BaseType_t delayed = xTaskDelayUntil(&last, period);
    lastWakeTicks            = static_cast<u32>(last);
    return delayed == pdTRUE;
#else
    // Starsze jądra mają tylko wariant void — przekroczenie wykrywamy sami.
    const TickType_t target  = last + period;
    const bool       onTime  = static_cast<i32>(xTaskGetTickCount() - target) < 0;
    vTaskDelayUntil(&last, period);
    lastWakeTicks = static_cast<u32>(onTime ? last : xTaskGetTickCount());
    return onTime;
#endif
}

u32 tickCount() { return static_cast<u32>(xTaskGetTickCount()); }

u32 ticksToMs(u32 ticks) {
    return static_cast<u32>((static_cast<u64>(ticks) * 1000ull) / configTICK_RATE_HZ);
}
u32 msToTicks(u32 ms) { return static_cast<u32>(pdMS_TO_TICKS(ms)); }

Millis nowMs() { return ticksToMs(tickCount()); }

Micros nowUs() {
#if HYDRA_PLAT_ESP32
    return static_cast<Micros>(esp_timer_get_time());
#elif HYDRA_PLAT_RP2
    return static_cast<Micros>(time_us_64());
#else
    // Vanilla FreeRTOS nie ma źródła mikrosekundowego — rozdzielczość tyknięcia.
    return static_cast<Micros>(nowMs()) * 1000ull;
#endif
}

void yield() { taskYIELD(); }

bool inIsr() {
#if HYDRA_PLAT_ESP32
    return xPortInIsrContext();
#elif defined(portCHECK_IF_IN_ISR)
    return portCHECK_IF_IN_ISR() != 0;
#else
    // Cortex-M: numer aktywnego wyjątku w IPSR; 0 = kontekst wątku.
    u32 ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
#endif
}

void startScheduler() {
#if HYDRA_MANUAL_SCHEDULER
    vTaskStartScheduler();
    // Powrót oznacza brak pamięci na task Idle — App zaloguje to jako błąd.
#endif
}

u32 stackHighWaterMark(TaskHandle h) {
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    return static_cast<u32>(uxTaskGetStackHighWaterMark(static_cast<TaskHandle_t>(h))) *
           sizeof(StackType_t);
#else
    HYDRA_UNUSED(h);
    return 0;
#endif
}

u32 taskCount() { return static_cast<u32>(uxTaskGetNumberOfTasks()); }

/**
 * Wolna sterta.
 *
 * Na RP2040/RP2350 rdzeń Philhowera używa przydziału opartego na malloc
 * (heap_3), który nie prowadzi własnej ewidencji i nie dostarcza
 * xPortGetFreeHeapSize. Miarodajną wartość zna tam warstwa rdzenia.
 */
#if HYDRA_PLAT_RP2
// Symbole konsolidatora pico-sdk wyznaczające obszar sterty. Deklarowane
// tutaj, a nie brane z RP2040Support.h, bo rdzeń frameworka nie może włączać
// nagłówków Arduino — pilnuje tego reguła zależności.
extern "C" char __StackLimit;
extern "C" char __bss_end__;
#endif

u32 freeHeapBytes() {
#if HYDRA_PLAT_RP2
    // Rdzeń Philhowera przydziela pamięć przez malloc (heap_3), który nie
    // prowadzi własnej ewidencji i nie dostarcza xPortGetFreeHeapSize.
    // Liczymy tak samo jak warstwa rdzenia: całość obszaru minus zajęte.
    const struct mallinfo info  = mallinfo();
    const size_t          total = static_cast<size_t>(&__StackLimit - &__bss_end__);
    const size_t          used  = static_cast<size_t>(info.uordblks);
    return static_cast<u32>(total > used ? total - used : 0);
#else
    return static_cast<u32>(xPortGetFreeHeapSize());
#endif
}

// ---------------------------------------------------------------------------
// Mutex — zawsze z dziedziczeniem priorytetów (rozdz. 10)
// ---------------------------------------------------------------------------

Mutex::Mutex() { h_ = xSemaphoreCreateMutex(); }

Mutex::~Mutex() {
    if (h_) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(h_));
    h_ = nullptr;
}

bool Mutex::lock(u32 timeoutMs) {
    if (!h_) return false;
    return xSemaphoreTake(static_cast<SemaphoreHandle_t>(h_), toTimeout(timeoutMs)) == pdTRUE;
}

bool Mutex::tryLock() {
    return h_ && xSemaphoreTake(static_cast<SemaphoreHandle_t>(h_), 0) == pdTRUE;
}

void Mutex::unlock() {
    if (h_) xSemaphoreGive(static_cast<SemaphoreHandle_t>(h_));
}

// ---------------------------------------------------------------------------
// Kolejka
// ---------------------------------------------------------------------------

Queue::~Queue() { destroy(); }

Status Queue::create(u32 length, u32 itemSize) {
    if (h_) return fail(Err::AlreadyExists);
    if (!length || !itemSize) return fail(Err::BadArgument);
    h_ = xQueueCreate(length, itemSize);
    if (!h_) return fail(Err::OutOfMemory);
    itemSize_ = itemSize;
    return ok();
}

void Queue::destroy() {
    if (h_) vQueueDelete(static_cast<QueueHandle_t>(h_));
    h_        = nullptr;
    itemSize_ = 0;
}

bool Queue::send(const void* item, u32 timeoutMs) {
    if (!h_ || !item) return false;
    return xQueueSend(static_cast<QueueHandle_t>(h_), item, toTimeout(timeoutMs)) == pdTRUE;
}

bool Queue::receive(void* out, u32 timeoutMs) {
    if (!h_ || !out) return false;
    return xQueueReceive(static_cast<QueueHandle_t>(h_), out, toTimeout(timeoutMs)) == pdTRUE;
}

bool Queue::sendFromIsr(const void* item, bool* woken) {
    if (!h_ || !item) return false;
    BaseType_t hpTaskWoken = pdFALSE;
    const BaseType_t rc =
        xQueueSendFromISR(static_cast<QueueHandle_t>(h_), item, &hpTaskWoken);
    if (woken) *woken = (hpTaskWoken == pdTRUE);

    // Sygnatura tego makra różni się między portami: na ESP32 nie przyjmuje
    // argumentów i wywołuje się je warunkowo, na portach ARM (RP2040/RP2350,
    // STM32) przyjmuje flagę i sam decyduje, czy przełączyć kontekst.
#if HYDRA_PLAT_ESP32
    if (hpTaskWoken == pdTRUE) portYIELD_FROM_ISR();
#else
    portYIELD_FROM_ISR(hpTaskWoken);
#endif
    return rc == pdTRUE;
}

u32 Queue::waiting() const {
    if (!h_) return 0;
    return static_cast<u32>(uxQueueMessagesWaiting(static_cast<QueueHandle_t>(h_)));
}

// ---------------------------------------------------------------------------
// Sekcja krytyczna
// ---------------------------------------------------------------------------

// ESP-IDF wymaga jawnego spinlocka (SMP), vanilla FreeRTOS maskuje przerwania.
#if HYDRA_PLAT_ESP32
namespace {
portMUX_TYPE gCriticalMux = portMUX_INITIALIZER_UNLOCKED;
}
#endif

CriticalSection::CriticalSection() {
#if HYDRA_PLAT_ESP32
    if (inIsr()) {
        portENTER_CRITICAL_ISR(&gCriticalMux);
        state_ = 1;
    } else {
        portENTER_CRITICAL(&gCriticalMux);
        state_ = 0;
    }
#else
    if (inIsr()) {
        state_ = static_cast<u32>(taskENTER_CRITICAL_FROM_ISR());
    } else {
        taskENTER_CRITICAL();
        state_ = 0xFFFFFFFFu;
    }
#endif
}

CriticalSection::~CriticalSection() {
#if HYDRA_PLAT_ESP32
    if (state_ == 1) {
        portEXIT_CRITICAL_ISR(&gCriticalMux);
    } else {
        portEXIT_CRITICAL(&gCriticalMux);
    }
#else
    if (state_ == 0xFFFFFFFFu) {
        taskEXIT_CRITICAL();
    } else {
        taskEXIT_CRITICAL_FROM_ISR(state_);
    }
#endif
}

}  // namespace rtos
}  // namespace hydra

#endif  // !HYDRA_PLAT_HOST
