cmake_minimum_required(VERSION 3.13)

if(DEFINED PICO_BOARD)
    message(DEBUG, "Using PICO_BOARD ${PICO_BOARD}")
else()
    set(PICO_BOARD "pico_w")
endif()

set(BLUEPAD32_ROOT "C:/pico/bluepad32" CACHE PATH "Bluepad32 root")
set(BTSTACK_ROOT ${PICO_SDK_PATH}/lib/btstack)

include(pico_sdk_import.cmake)

project(ds4_bridge C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(ds4_bridge
    src/main.c
    src/my_platform.c
)

target_include_directories(ds4_bridge PRIVATE
    src
    ${BLUEPAD32_ROOT}/src/components/bluepad32/include
)

include_directories(ds4_bridge src)
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include)
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include)
include_directories(${BTSTACK_ROOT}/src)

target_link_libraries(ds4_bridge PUBLIC
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_cyw43
    hardware_pwm
    bluepad32
)

add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)

pico_enable_stdio_usb(ds4_bridge 1)
pico_enable_stdio_uart(ds4_bridge 0)

pico_add_extra_outputs(ds4_bridge)
