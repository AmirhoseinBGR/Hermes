// STM32 glue for the library console. All console logic lives in
// gsm/cli/cli.{hpp,cpp} (library feature); this file only wires the hardware.
#include "cli.hpp"
#include "gsm/cli/cli.hpp"
#include "gsm/ports/stm32/gsm_port_stm32.hpp"
#include "stm32h7xx_hal.h"
#include "usart.h"
#include <cstring>

using namespace gsm;

namespace cli
{

// ---- hardware instances -------------------------------------------------
static port::Stm32Uart<512> s_gsm_uart(&huart7);
static port::Stm32Uart<256> s_dbg_uart(&huart1);
static port::Stm32Clock     s_clk;
#if defined(GSM_PWR_GPIO_Port) && defined(GSM_PWR_Pin)
static port::Stm32PowerControl s_gsmpwr(GSM_PWR_GPIO_Port, GSM_PWR_Pin);
static Modem                   s_modem(s_gsm_uart, s_clk, kQuectelM08R, &s_gsmpwr);
#else
static Modem s_modem(s_gsm_uart, s_clk, kQuectelM08R, nullptr);
#endif

static void uart_write(void*, const char* data, uint16_t len)
{ s_dbg_uart.write(reinterpret_cast<const uint8_t*>(data), len); }

static gsm::cli::Console s_console(s_modem, s_clk);

void init()
{
    s_gsm_uart.begin();
    s_dbg_uart.begin();

    s_console.set_output(gsm::cli::TextSink{&uart_write, nullptr});

    s_modem.housekeeping().signal_ms     = 15000;
    s_modem.housekeeping().sms_scan_ms   = 20000;
    s_modem.housekeeping().full_sweep_ms = 60000;
    s_modem.housekeeping().cleanup_ms    = 300000;

    s_modem.begin();
    s_console.init();
}

void tick()
{
    // Console input is buffered by the RX ISR (DMA -> ring) and executed here,
    // in the main loop. A command such as 'K' (PWRKEY hard reset) must never run
    // inside an interrupt: it restarts the modem FSM and used to block on
    // HAL_Delay() inside the ISR, which froze the whole device.
    uint8_t  b      = 0;
    uint16_t budget = 256;
    while (budget-- && s_dbg_uart.read(&b, 1) == 1) s_console.feed(b);

    s_modem.tick();
    s_console.tick();
}

void on_uart_rx(UART_HandleTypeDef* huart, uint16_t size)
{
    if (huart->Instance == UART7)
    {
        s_gsm_uart.on_rx_event(size);
    }
    else if (huart->Instance == USART1)
    {
        s_dbg_uart.on_rx_event(size); // ISR: DMA -> ring only, no command parsing
    }
}

void on_uart_error(UART_HandleTypeDef* huart)
{
    if (huart->Instance == UART7)
        s_gsm_uart.on_error();
    else if (huart->Instance == USART1)
        s_dbg_uart.on_error();
}

} // namespace cli
