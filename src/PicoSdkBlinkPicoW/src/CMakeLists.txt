add_executable(pico_sdk_blink_pico_w main.c)

target_link_libraries(pico_sdk_blink_pico_w
    pico_stdlib
    pico_cyw43_arch_none   # CYW43 in no-WiFi mode — gives access to LED only
)

pico_set_program_name(pico_sdk_blink_pico_w "PicoSdkBlinkPicoW")
pico_set_program_version(pico_sdk_blink_pico_w "1.0")

pico_enable_stdio_usb(pico_sdk_blink_pico_w 1)
pico_enable_stdio_uart(pico_sdk_blink_pico_w 0)

pico_add_extra_outputs(pico_sdk_blink_pico_w)
