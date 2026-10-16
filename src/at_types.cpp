#include "gsm/core/at_types.hpp"

namespace gsm
{

const char* to_string(AtStatus s)
{
    switch (s)
    {
    case AtStatus::Pending:
        return "PENDING";
    case AtStatus::Ok:
        return "OK";
    case AtStatus::Error:
        return "ERROR";
    case AtStatus::CmeError:
        return "CME_ERROR";
    case AtStatus::CmsError:
        return "CMS_ERROR";
    case AtStatus::Timeout:
        return "TIMEOUT";
    case AtStatus::Aborted:
        return "ABORTED";
    case AtStatus::NoDialtone:
        return "NO_DIALTONE";
    case AtStatus::Busy:
        return "BUSY";
    case AtStatus::NoCarrier:
        return "NO_CARRIER";
    case AtStatus::NoAnswer:
        return "NO_ANSWER";
    }
    return "?";
}

const char* cms_error_hint(int16_t code)
{
    switch (code)
    {
    case 310:
        return "SIM not inserted";
    case 311:
        return "SIM PIN required";
    case 313:
        return "SIM failure";
    case 321:
        return "invalid memory index";
    case 322:
        return "memory full";
    case 330:
        return "SMSC address unknown";
    case 500:
        return "unknown error";
    case 512:
        return "SMS send failed (network/SMSC)";
    case 520:
        return "malformed payload: CRLF sent before Ctrl+Z";
    case 3518:
        return "destination not UCS2-encoded while CSCS=UCS2";
    default:
        return "see module CMS error table";
    }
}

const char* cme_error_hint(int16_t code)
{
    switch (code)
    {
    case 10:
        return "SIM not inserted";
    case 11:
        return "SIM PIN required";
    case 13:
        return "SIM failure";
    case 30:
        return "no network service";
    case 58:
        return "invalid parameters / stuck in data-entry prompt";
    case 100:
        return "unknown";
    default:
        return "see module CME error table";
    }
}

} // namespace gsm
