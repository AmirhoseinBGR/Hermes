// STM32 Example App Entry Point
// Keeps app logic minimal and delegates interactive testing to cli module
#include "app.hpp"
#include "cli.hpp"
#include "usart.h"

extern "C" int AppInit()
{
    cli::init();
    return 0;
}

extern "C" int AppRun()
{
    cli::tick();
    return 0;
}

// CubeMX UART DMA Event Callbacks
extern "C" void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size)
{ cli::on_uart_rx(huart, size); }

extern "C" void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)
{ cli::on_uart_error(huart); }
