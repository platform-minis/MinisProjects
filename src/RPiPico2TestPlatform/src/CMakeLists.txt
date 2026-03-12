# ---------------------------------------------------------------------------
# Firmware sources (compiled only for Pico target)
# ---------------------------------------------------------------------------

# Generate PIO header from capture.pio
pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/capture.pio)

add_executable(${PROJECT_NAME}
    main.cpp
    protocol.cpp
    gpio_ctrl.cpp
    capture.cpp
    sequence.cpp
    pwm_ctrl.cpp
    command_handler.cpp
    pico_hal.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${PROJECT_NAME}
    pico_stdlib
    pico_stdio_usb
    hardware_gpio
    hardware_pwm
    hardware_pio
    hardware_timer
    hardware_clocks
)

# Enable USB output; disable UART output
pico_enable_stdio_usb(${PROJECT_NAME}  1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Generate .uf2 / .bin / .hex / .map
pico_add_extra_outputs(${PROJECT_NAME})
