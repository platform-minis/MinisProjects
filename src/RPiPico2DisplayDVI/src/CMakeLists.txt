add_executable(rpi_pico2_display_dvi
    main.c
)

target_include_directories(rpi_pico2_display_dvi PRIVATE
    ${picodvi_SOURCE_DIR}/software/libdvi
    ${picodvi_SOURCE_DIR}/software/include
)

# 1bpp monochrome: encode once, use the same TMDS symbols on all 3 lanes
target_compile_definitions(rpi_pico2_display_dvi PRIVATE
    DVI_MONOCHROME_TMDS=1
)

target_link_libraries(rpi_pico2_display_dvi
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_interp
    hardware_vreg
    libdvi
)

pico_set_program_name(rpi_pico2_display_dvi "RPiPico2DisplayDVI")
pico_set_program_version(rpi_pico2_display_dvi "0.1")

pico_enable_stdio_usb(rpi_pico2_display_dvi 1)
pico_enable_stdio_uart(rpi_pico2_display_dvi 0)

# pico_add_extra_outputs disabled: bundled picotool too old for rp2350
# Use: picotool uf2 convert rpi_pico2_display_dvi.elf rpi_pico2_display_dvi.uf2 --family rp2350-arm-s
