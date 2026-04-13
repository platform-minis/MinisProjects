add_executable(pico_sdk_blink_pico main.c)

target_link_libraries(pico_sdk_blink_pico
    pico_stdlib
)

pico_set_program_name(pico_sdk_blink_pico "PicoSdkBlinkPico")
pico_set_program_version(pico_sdk_blink_pico "1.0")

pico_enable_stdio_usb(pico_sdk_blink_pico 1)
pico_enable_stdio_uart(pico_sdk_blink_pico 0)

pico_add_extra_outputs(pico_sdk_blink_pico)
