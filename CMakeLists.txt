cmake_minimum_required(VERSION 3.20)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(CMAKE_OSX_ARCHITECTURES "")
project(blinky C ASM)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(MCPU cortex-m4)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

add_executable(blinky.elf
    src/main.c
    startup_stm32f411.s
)

target_compile_options(blinky.elf PRIVATE
    -mcpu=${MCPU}
    -mthumb
    -O2
    -ffreestanding
    -Wall
)

target_link_options(blinky.elf PRIVATE
    -T${CMAKE_SOURCE_DIR}/linker.ld
    -nostdlib
    -Wl,--gc-sections
    -mcpu=${MCPU}
    -mthumb
)

add_custom_command(TARGET blinky.elf POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary
            blinky.elf blinky.bin
)
