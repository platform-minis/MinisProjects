add_executable(rpi_pico2_display_dvi
    main.c
)

target_link_libraries(rpi_pico2_display_dvi
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_gpio
    hardware_irq
)

pico_set_program_name(rpi_pico2_display_dvi "RPiPico2DisplayDVI")
pico_set_program_version(rpi_pico2_display_dvi "0.1")

# USB stdio for debug; UART disabled
pico_enable_stdio_usb(rpi_pico2_display_dvi 1)
pico_enable_stdio_uart(rpi_pico2_display_dvi 0)

pico_add_extra_outputs(rpi_pico2_display_dvi)
