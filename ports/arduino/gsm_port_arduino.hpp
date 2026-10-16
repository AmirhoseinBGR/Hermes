#pragma once
// Arduino port: any Stream (HardwareSerial / SoftwareSerial) works.
#include "gsm/hal/hal.hpp"
#include <Arduino.h>

namespace gsm::port
{

class ArduinoUart final : public hal::IUart
{
  public:
    explicit ArduinoUart(Stream& stream) : s_(stream) {}
    uint16_t write(const uint8_t* data, uint16_t len) override
    { return static_cast<uint16_t>(s_.write(data, len)); }
    uint16_t read(uint8_t* out, uint16_t max) override
    {
        uint16_t n = 0;
        while (n < max && s_.available() > 0)
        {
            const int c = s_.read();
            if (c < 0) break;
            out[n++] = static_cast<uint8_t>(c);
        }
        return n;
    }
    uint16_t available() override
    {
        const int a = s_.available();
        return a > 0 ? static_cast<uint16_t>(a) : 0;
    }
    void drain_rx() override
    {
        while (s_.available() > 0)
            s_.read();
    }

  private:
    Stream& s_;
};

class ArduinoClock final : public hal::IClock
{
  public:
    uint32_t millis() override { return ::millis(); }
};

} // namespace gsm::port
