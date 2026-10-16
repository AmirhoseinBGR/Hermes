#include "gsm/core/urc_router.hpp"
#include "gsm/util/static_string.hpp"

namespace gsm
{

Urc UrcRouter::classify(const char* line, int32_t& arg)
{
    arg = -1;
    if (!line || !*line) return Urc::Unknown;

    if (str_starts_with(line, "RING")) return Urc::Ring;
    if (str_starts_with(line, "+CLIP:")) return Urc::CallerId;
    if (str_starts_with(line, "NO CARRIER")) return Urc::CallEnded;
    if (str_starts_with(line, "BUSY")) return Urc::CallBusy;
    if (str_starts_with(line, "NO ANSWER")) return Urc::NoAnswer;
    if (str_starts_with(line, "+CMTI:"))
    {
        parse_int(line + 6, arg);
        return Urc::SmsReceived;
    }
    if (str_starts_with(line, "+CDSI:") || str_starts_with(line, "+CDS:"))
        return Urc::SmsDeliveryRep;
    if (str_starts_with(line, "+CPIN: READY")) return Urc::SimReady;
    if (str_starts_with(line, "+CPIN: NOT READY")) return Urc::SimRemoved;
    if (str_starts_with(line, "+CPIN: NOT INSERTED")) return Urc::SimRemoved;
    if (str_starts_with(line, "+CPIN:"))
    {
        // Other CPIN states: SIM PIN, SIM PUK, etc.
        return Urc::SimReady;
    }
    // Hardware/modem SIM detection URCs
    if (str_starts_with(line, "+SIM: 0") || str_starts_with(line, "+CPIN: NOT INSERTED") ||
        str_starts_with(line, "+QSIM: 0"))
        return Urc::SimRemoved;
    if (str_starts_with(line, "+SIM: 1") || str_starts_with(line, "+QSIM: 1"))
        return Urc::SimInserted;

    if (str_starts_with(line, "Call Ready")) return Urc::CallReady;
    if (str_starts_with(line, "SMS Ready")) return Urc::SmsReady;
    if (str_starts_with(line, "NORMAL POWER DOWN") || str_starts_with(line, "POWERED DOWN"))
        return Urc::PowerDown;
    if (str_starts_with(line, "+CREG:") || str_starts_with(line, "+CGREG:"))
    {
        const char* colon = std::strchr(line, ':');
        if (colon)
        {
            char f[8];
            if (csv_field(colon + 1, 1, f, sizeof(f))) { parse_int(f, arg); }
            else if (csv_field(colon + 1, 0, f, sizeof(f))) { parse_int(f, arg); }
        }
        return Urc::NetworkReg;
    }
    if (str_starts_with(line, "+CUSD:")) return Urc::Ussd;
    if (str_starts_with(line, "RDY") || str_starts_with(line, "DY")) return Urc::ModemReady;
    return Urc::Unknown;
}

bool UrcRouter::dispatch(const char* line, uint16_t len)
{
    int32_t   arg  = -1;
    const Urc kind = classify(line, arg);
    if (kind == Urc::Unknown) return false;
    (void)len;
    UrcEvent ev{kind, line, arg};
    if (handler_) handler_(ev);
    return true;
}

} // namespace gsm
