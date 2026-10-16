#pragma once
#include "gsm/util/delegate.hpp"
#include <cstdint>

namespace gsm
{

enum class Urc : uint8_t
{
    Unknown = 0,
    Ring,
    CallerId,
    CallEnded,
    CallBusy,
    NoAnswer,
    SmsReceived,
    SmsDeliveryRep,
    SimReady,
    SimRemoved,
    SimInserted,
    CallReady,
    SmsReady,
    PowerDown,
    NetworkReg,
    SignalReport,
    Ussd,
    ModemReady,
};

struct UrcEvent
{
    Urc         kind = Urc::Unknown;
    const char* line = nullptr;
    int32_t     arg  = -1;
};

class UrcRouter
{
  public:
    using Handler = Delegate<const UrcEvent&>;

    void       set_handler(Handler h) { handler_ = h; }
    bool       dispatch(const char* line, uint16_t len);
    static Urc classify(const char* line, int32_t& arg);

  private:
    Handler handler_;
};

} // namespace gsm
