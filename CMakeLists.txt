# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(pico_angry_birds C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(pico_angry_birds)

add_compile_options(-Ofast)

# must match with pio filename and executable name from above
pico_generate_pio_header(pico_angry_birds ${CMAKE_CURRENT_LIST_DIR}/VGA/hsync.pio)
pico_generate_pio_header(pico_angry_birds ${CMAKE_CURRENT_LIST_DIR}/VGA/vsync.pio)
pico_generate_pio_header(pico_angry_birds ${CMAKE_CURRENT_LIST_DIR}/VGA/rgb.pio)

# must match with executable name and source file names
target_sources(pico_angry_birds PRIVATE
               main.c game.c input.c physics.c levels.c
               gfx.c background.c render.c fx.c
               VGA/vga16_graphics_v3.c)

# Bigger main stack; core 1 gets its own stack array in main.c.
# -fno-math-errno lets the compiler use the FPU's sqrt/rounding instructions directly.
target_compile_definitions(pico_angry_birds PRIVATE PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0)
target_compile_options(pico_angry_birds PRIVATE -fno-math-errno)

# must match with executable name
target_link_libraries(  pico_angry_birds
                        PRIVATE
                        pico_stdlib
                        pico_divider
                        pico_multicore
                        pico_sync
                        pico_bootsel_via_double_reset
                        hardware_pio
                        hardware_dma
                        hardware_adc
                        hardware_irq
                        hardware_clocks
                        hardware_pll)

# must match with executable name
pico_add_extra_outputs(pico_angry_birds)
