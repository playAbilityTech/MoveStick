cmake_minimum_required(VERSION 3.13)

add_subdirectory(usb_midi_host)

add_compile_definitions(PICO_DEFAULT_UART_BAUD_RATE=921600)
add_compile_definitions(PERSISTED_CONFIG_SIZE=4096)
add_compile_definitions(PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/tinyusb")
set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/Pico-PIO-USB")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/src/boards")

include(pico_sdk_import.cmake)

project(remapper)

pico_sdk_init()

check_and_add_pico_pio_usb_support()

add_compile_options(-Wall)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)

if((PICO_BOARD STREQUAL "pico") OR (PICO_BOARD STREQUAL "pico2") OR (PICO_BOARD STREQUAL "pico_w") OR (PICO_BOARD STREQUAL "pico2_w"))
add_compile_definitions(GPIO_VALID_PINS_BASE=0b00011100011111111111111111111111)
endif()


add_executable(remapper
    src/main.cc
    src/remapper.cc
    src/remapper_single.cc
    src/crc.cc
    src/descriptor_parser.cc
    src/tinyusb_stuff.cc
    src/our_descriptor.cc
    src/globals.cc
    src/config.cc
    src/quirks.cc
    src/interval_override.cc
    src/out_report.cc
    src/tick.cc
    src/activity_led.cc
    src/ps_auth.cc
    src/app_driver.cc
    src/xbox.cc
)

if((PICO_BOARD STREQUAL "pico") OR (PICO_BOARD STREQUAL "pico2"))
target_compile_definitions(remapper PUBLIC PICO_DEFAULT_UART_TX_PIN=16)
target_compile_definitions(remapper PUBLIC PICO_DEFAULT_UART_RX_PIN=17)
target_compile_definitions(remapper PUBLIC PICO_DEFAULT_PIO_USB_DP_PIN=0)
endif()

target_include_directories(remapper PRIVATE
    src
    src/tusb_config_both
    ${PICO_PIO_USB_PATH}
)

target_link_libraries(remapper
    pico_stdlib
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_flash
    $<$<STREQUAL:${PICO_BOARD},remapper_v8>:hardware_adc>
    tinyusb_device
    tinyusb_host
    tinyusb_board
    tinyusb_pico_pio_usb
    usb_midi_host
)

pico_set_binary_type(remapper copy_to_ram)
if(PICO_PLATFORM STREQUAL "rp2040")
    pico_set_linker_script(remapper ${CMAKE_CURRENT_LIST_DIR}/remapper_single.ld)
elseif(PICO_PLATFORM STREQUAL "rp2350-arm-s")
    pico_set_linker_script(remapper ${CMAKE_CURRENT_LIST_DIR}/remapper_single_rp2350.ld)
else()
    message(FATAL_ERROR "Unknown PICO_PLATFORM.")
endif()
pico_add_extra_outputs(remapper)


add_executable(remapper_dual_a
    src/main.cc
    src/remapper.cc
    src/remapper_dual_a.cc
    src/crc.cc
    src/descriptor_parser.cc
    src/tinyusb_stuff.cc
    src/our_descriptor.cc
    src/globals.cc
    src/config.cc
    src/quirks.cc
    src/interval_override.cc
    src/serial.cc
    src/tick.cc
    src/activity_led.cc
    src/pico_debug/swd.c
    src/pico_debug/flash.c
    src/pico_debug/adi.c
    dual_b_binary.c
    src/ps_auth.cc
    $<$<STREQUAL:${PICO_BOARD},remapper_v7>:src/i2c.cc>
    $<$<STREQUAL:${PICO_BOARD},remapper_v7>:src/mcp4651.cc>
)
target_include_directories(remapper_dual_a PRIVATE
    src
    src/tusb_config_device
    src/pico_debug
)
target_link_libraries(remapper_dual_a
    pico_stdlib
    hardware_flash
    $<$<STREQUAL:${PICO_BOARD},remapper_v7>:hardware_i2c>
    hardware_pio
    tinyusb_device
    tinyusb_board
    pico_unique_id
)
pico_generate_pio_header(remapper_dual_a ${CMAKE_CURRENT_LIST_DIR}/src/pico_debug/swd.pio)
pico_add_extra_outputs(remapper_dual_a)

add_executable(remapper_dual_b
    src/remapper_dual_b.cc
    src/crc.cc
    src/interval_override.cc
    src/serial.cc
    src/out_report.cc
    src/activity_led.cc
    src/app_driver.cc
    src/xbox.cc
)
target_include_directories(remapper_dual_b PRIVATE
    src
    src/tusb_config_host
)
target_link_libraries(remapper_dual_b
    pico_stdlib
    tinyusb_host
    tinyusb_board
    usb_midi_host
)
pico_add_extra_outputs(remapper_dual_b)
pico_add_hex_output(remapper_dual_b)

add_custom_command(
  OUTPUT dual_b_binary.c dual_b_binary.h
  DEPENDS remapper_dual_b
  COMMAND srec_cat remapper_dual_b.hex -Intel -Output dual_b_binary.c -C-Array dual_b_binary -INClude
  VERBATIM
)

add_executable(remapper_serial
    src/main.cc
    src/remapper.cc
    src/remapper_serial.cc
    src/crc.cc
    src/descriptor_parser.cc
    src/tinyusb_stuff.cc
    src/our_descriptor.cc
    src/globals.cc
    src/config.cc
    src/quirks.cc
    src/interval_override.cc
    src/tick.cc
    src/activity_led.cc
    src/ps_auth.cc
)
target_include_directories(remapper_serial PRIVATE
    src
    src/tusb_config_device
)
target_link_libraries(remapper_serial
    pico_stdlib
    pico_unique_id
    hardware_flash
    tinyusb_device
    tinyusb_board
)
pico_add_extra_outputs(remapper_serial)

if((PICO_BOARD STREQUAL "pico_w") OR (PICO_BOARD STREQUAL "pico2_w"))
set(BTSTACK_MARKER "${PICO_SDK_PATH}/lib/btstack/src/bluetooth.h")
if(NOT EXISTS "${BTSTACK_MARKER}")
    message(FATAL_ERROR
        "BTstack is not initialized (missing ${BTSTACK_MARKER}).\n"
        "Run: git submodule update --init --recursive firmware/pico-sdk")
endif()
add_executable(remapper_bluetooth
    src/main.cc
    src/remapper.cc
    src/remapper_bluetooth.cc
    src/ble_nus.cc
    src/nus_protocol.cc
    src/ble_shared.cc
    src/ble_button_led.cc
    src/crc.cc
    src/descriptor_parser.cc
    src/tinyusb_stuff.cc
    src/our_descriptor.cc
    src/globals.cc
    src/config.cc
    src/quirks.cc
    src/interval_override.cc
    src/tick.cc
    src/ps_auth.cc
)
target_compile_definitions(remapper_bluetooth PUBLIC
    REMAPPER_BLUETOOTH=1
    REMAPPER_BLUETOOTH_NUS_ONLY=1
    CYW43_ENABLE_BLUETOOTH=1
    PICO_FLASH_BANK_TOTAL_SIZE=16384
)
target_include_directories(remapper_bluetooth PRIVATE
    src
    src/tusb_config_device
    ${PICO_SDK_PATH}/lib/btstack/src/ble/gatt-service
)
target_link_libraries(remapper_bluetooth
    pico_stdlib
    pico_unique_id
    hardware_flash
    hardware_gpio
    hardware_sync
    pico_util
    tinyusb_device
    tinyusb_board
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
)
pico_btstack_make_gatt_header(remapper_bluetooth PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src/ble_remapper.gatt
    -I${PICO_SDK_PATH}/lib/btstack/src/ble/gatt-service)
if(COMMAND suppress_btstack_warnings)
    suppress_btstack_warnings()
endif()
pico_add_extra_outputs(remapper_bluetooth)
endif()

add_executable(flash_b_side
    src/flash_b_side.cc
    dual_b_binary.c
    src/pico_debug/swd.c
    src/pico_debug/flash.c
    src/pico_debug/adi.c
)
target_include_directories(flash_b_side PRIVATE
    src
    src/pico_debug
)
target_link_libraries(flash_b_side
    pico_stdlib
    hardware_pio
)
pico_generate_pio_header(flash_b_side ${CMAKE_CURRENT_LIST_DIR}/src/pico_debug/swd.pio)
pico_add_extra_outputs(flash_b_side)
pico_set_binary_type(flash_b_side no_flash)

add_custom_command(
    OUTPUT remapper_dual_combined.uf2
    DEPENDS remapper_dual_a flash_b_side
    COMMAND ${CMAKE_CURRENT_LIST_DIR}/combine_uf2.py remapper_dual_a.uf2 flash_b_side.uf2 remapper_dual_combined.uf2
    VERBATIM
)

add_custom_target(remapper_dual_combined ALL
    DEPENDS remapper_dual_combined.uf2
)
