#pragma once
#include "gsm/hal/hal.hpp"
#include "gsm/util/ring_buffer.hpp"
#include <cstring>

namespace gsm::port
{

template <uint16_t Size = 2048> class HostUart final : public hal::IUart
{
  public:
    uint16_t write(const uint8_t* data, uint16_t len) override
    {
        for (uint16_t i = 0; i < len && tx_len_ + 1u < sizeof(tx_); ++i)
            tx_[tx_len_++] = static_cast<char>(data[i]);
        tx_[tx_len_] = '\0';
        return len;
    }
    uint16_t read(uint8_t* out, uint16_t max) override
    {
        uint16_t n = 0;
        uint8_t  b = 0;
        while (n < max && rx_.pop(b))
            out[n++] = b;
        return n;
    }
    uint16_t available() override { return rx_.size(); }
    void     drain_rx() override { rx_.clear(); }

    void inject(const char* text)
    { rx_.push(reinterpret_cast<const uint8_t*>(text), static_cast<uint16_t>(std::strlen(text))); }
    const char* tx() const { return tx_; }
    void        clear_tx()
    {
        tx_len_ = 0;
        tx_[0]  = '\0';
    }

  private:
    RingBuffer<Size> rx_;
    char             tx_[Size]{};
    uint16_t         tx_len_ = 0;
};

class HostClock final : public hal::IClock
{
  public:
    uint32_t millis() override { return now; }
    uint32_t now = 0;
};

class HostPowerControl final : public hal::IPowerControl
{
  public:
    void power_key(bool asserted) override
    {
        level = asserted;
        if (asserted) ++pulse_count;
    }
    uint32_t pulse_count = 0;
    bool     level       = false; // current PWRKEY line level
};

} // namespace gsm::port
