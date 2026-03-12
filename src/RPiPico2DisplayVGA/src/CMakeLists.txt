add_executable(rpi_pico2_display_vga
    main.c
)

# add_picovga() zastępuje target_link_libraries dla PicoVGA:
#   - linkuje bibliotekę i wszystkie jej zależności pico-sdk
#   - dodaje ścieżki include (vga.h dostępny bez prefiksu ścieżki)
#   - generuje kod PIO i ASM inline
add_picovga(rpi_pico2_display_vga)

# PS/2 keyboard (PIO1, GP20/GP21)
target_link_libraries(rpi_pico2_display_vga PRIVATE ps2kbd-lib)

pico_set_program_name(rpi_pico2_display_vga "RPiPico2DisplayVGA")
pico_set_program_version(rpi_pico2_display_vga "0.1")

# USB stdio dla debugowania; UART wyłączony
pico_enable_stdio_usb(rpi_pico2_display_vga 1)
pico_enable_stdio_uart(rpi_pico2_display_vga 0)

pico_add_extra_outputs(rpi_pico2_display_vga)
