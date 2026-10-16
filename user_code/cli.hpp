#pragma once

#include "main.h"

namespace cli
{

// Thin STM32 glue around the library console (gsm::cli::Console).
void init();
void tick();

// HAL UART event handlers
void on_uart_rx(UART_HandleTypeDef* huart, uint16_t size);
void on_uart_error(UART_HandleTypeDef* huart);

} // namespace cli
