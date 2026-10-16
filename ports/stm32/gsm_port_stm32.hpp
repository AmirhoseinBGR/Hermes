#pragma once
#include "gsm/hal/hal.hpp"
#include "gsm/util/ring_buffer.hpp"
#include "main.h"

namespace gsm::port
{

template <uint16_t DmaSize = 256, uint16_t RingSize = GSM_RX_RING_SIZE>
class Stm32Uart final : public hal::IUart
{
  public:
    explicit Stm32Uart(UART_HandleTypeDef* huart, uint32_t tx_timeout_ms = 100)
        : huart_(huart), tx_timeout_(tx_timeout_ms)
    {
    }

    void                begin() { start_rx(); }
    UART_HandleTypeDef* handle() const { return huart_; }

    uint16_t write(const uint8_t* data, uint16_t len) override
    {
        if (!data || !len) return 0;
        return HAL_UART_Transmit(huart_, const_cast<uint8_t*>(data), len, tx_timeout_) == HAL_OK
                   ? len
                   : 0;
    }

    uint16_t read(uint8_t* out, uint16_t max) override
    {
        uint16_t n = 0;
        uint8_t  b = 0;
        while (n < max && ring_.pop(b))
            out[n++] = b;
        return n;
    }

    uint16_t available() override { return ring_.size(); }
    void     drain_rx() override { ring_.clear(); }

    void on_rx_event(uint16_t size)
    {
        if (size == last_pos_) return;
        if (size > last_pos_)
        {
            ring_.push(dma_ + last_pos_, static_cast<uint16_t>(size - last_pos_));
        }
        else
        {
            ring_.push(dma_ + last_pos_, static_cast<uint16_t>(DmaSize - last_pos_));
            if (size) ring_.push(dma_, size);
        }
        last_pos_ = (size >= DmaSize) ? 0 : size;
        if (!circular_)
        {
            last_pos_ = 0;
            start_rx();
        }
    }

    void on_error()
    {
        __HAL_UART_CLEAR_OREFLAG(huart_);
        __HAL_UART_CLEAR_NEFLAG(huart_);
        __HAL_UART_CLEAR_FEFLAG(huart_);
        last_pos_ = 0;
        start_rx();
    }

    void set_circular(bool on) { circular_ = on; }

  private:
    void start_rx()
    {
        HAL_UARTEx_ReceiveToIdle_DMA(huart_, dma_, DmaSize);
        if (huart_->hdmarx) __HAL_DMA_DISABLE_IT(huart_->hdmarx, DMA_IT_HT);
    }

    UART_HandleTypeDef*  huart_;
    uint32_t             tx_timeout_;
    uint8_t              dma_[DmaSize]{};
    volatile uint16_t    last_pos_ = 0;
    bool                 circular_ = true;
    RingBuffer<RingSize> ring_;
};

class Stm32Clock final : public hal::IClock
{
  public:
    uint32_t millis() override { return HAL_GetTick(); }
};

class Stm32PowerControl final : public hal::IPowerControl
{
  public:
    Stm32PowerControl(GPIO_TypeDef* pwr_port, uint16_t pwr_pin, GPIO_TypeDef* rst_port = nullptr,
                      uint16_t rst_pin = 0, bool active_low = true)
        : pwr_port_(pwr_port), pwr_pin_(pwr_pin), rst_port_(rst_port), rst_pin_(rst_pin),
          active_low_(active_low)
    {
    }

    // Level-only driver. NO HAL_Delay here on purpose: Modem::tick() owns the
    // pulse timing (assert -> hold GSM_PWRKEY_PULSE_MS -> release -> boot
    // guard). Any blocking wait in this call would hang the whole firmware when
    // it is reached from an interrupt (e.g. a console command fed from the UART
    // RX ISR), because HAL_Delay() spins on the SysTick tick, which cannot
    // advance while a lower-priority SysTick is masked by the active IRQ.
    void power_key(bool asserted) override
    {
        if (!pwr_port_) return;
        HAL_GPIO_WritePin(pwr_port_, pwr_pin_,
                          (asserted ^ active_low_) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    void reset(bool asserted) override
    {
        if (!rst_port_) return;
        HAL_GPIO_WritePin(rst_port_, rst_pin_,
                          (asserted ^ active_low_) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

  private:
    GPIO_TypeDef* pwr_port_;
    uint16_t      pwr_pin_;
    GPIO_TypeDef* rst_port_;
    uint16_t      rst_pin_;
    bool          active_low_;
};

} // namespace gsm::port
