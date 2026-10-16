#pragma once
#include <cstdint>

namespace gsm::hal
{

class IUart
{
  public:
    virtual ~IUart()                                          = default;
    virtual uint16_t write(const uint8_t* data, uint16_t len) = 0;
    virtual uint16_t read(uint8_t* out, uint16_t max)         = 0;
    virtual uint16_t available()                              = 0;
    virtual void     drain_rx()                               = 0;
    virtual bool     set_baud(uint32_t) { return false; }
};

class IClock
{
  public:
    virtual ~IClock()         = default;
    virtual uint32_t millis() = 0;
};

class IPowerControl
{
  public:
    virtual ~IPowerControl() = default;
    virtual void power_key(bool asserted) { (void)asserted; }
    virtual void reset(bool asserted) { (void)asserted; }
    virtual bool status() { return true; }
};

inline bool elapsed(uint32_t now, uint32_t since, uint32_t span)
{ return static_cast<uint32_t>(now - since) >= span; }

} // namespace gsm::hal
