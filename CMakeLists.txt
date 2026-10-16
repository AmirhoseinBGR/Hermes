cmake_minimum_required(VERSION 3.16)
project(gsm_modem CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(gsm_modem STATIC
    src/log.cpp
    src/at_types.cpp
    src/line_reader.cpp
    src/urc_router.cpp
    src/at_engine.cpp
    src/ucs2.cpp
    src/phone_number.cpp
    src/sms_service.cpp
    src/call_service.cpp
    src/status_service.cpp
    src/ussd_service.cpp
    src/job_queue.cpp
    src/watchdog.cpp
    src/modem.cpp
    src/cli.cpp

    
)
target_include_directories(gsm_modem PUBLIC include)

target_compile_options(gsm_modem PRIVATE
    -Wall -Wextra -Wshadow -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)

option(GSM_PORT_STM32 "Build the STM32 HAL port" OFF)
if(GSM_PORT_STM32)
    target_include_directories(gsm_modem PUBLIC ports/stm32)
endif()

option(GSM_BUILD_TESTS "Build the host smoke test" OFF)
if(GSM_BUILD_TESTS)
    add_executable(host_smoke tests/host_smoke.cpp)
    target_link_libraries(host_smoke PRIVATE gsm_modem)
    target_include_directories(host_smoke PRIVATE ports/host)
    enable_testing()
    add_test(NAME host_smoke COMMAND host_smoke)
endif()
