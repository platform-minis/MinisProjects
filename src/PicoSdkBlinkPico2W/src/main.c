#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

// Pico 2 W: built-in LED is connected to the CYW43 wireless chip, not directly to GPIO
// Use cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, ...) to control it

int main(void) {
    stdio_init_all();

    // Initialise CYW43 in no-WiFi mode (only needed for LED access)
    if (cyw43_arch_init()) {
        // Init failed — fall back to tight loop (no LED)
        while (true) tight_loop_contents();
    }

    while (true) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(500);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(500);
    }
}
