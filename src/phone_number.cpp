#include "gsm/codec/phone_number.hpp"
#include <cstring>

namespace gsm
{

bool PhoneNumber::assign(const char* raw, const char* country_code)
{
    value_.clear();
    if (!raw) return false;
    char     digits[GSM_SMS_NUMBER_BYTES];
    uint16_t n    = 0;
    bool     plus = false;
    for (const char* p = raw; *p && n + 1u < sizeof(digits); ++p)
    {
        if (*p == '+' && n == 0)
        {
            plus = true;
            continue;
        }
        if (*p >= '0' && *p <= '9') digits[n++] = *p;
    }
    digits[n] = '\0';
    if (n == 0) return false;

    const char*    cc        = country_code ? country_code : "+98";
    const char*    cc_digits = (*cc == '+') ? cc + 1 : cc;
    const uint16_t cc_len    = static_cast<uint16_t>(std::strlen(cc_digits));

    if (plus) { value_.printf("+%s", digits); }
    else if (n > cc_len && std::memcmp(digits, cc_digits, cc_len) == 0)
    {
        value_.printf("+%s", digits);
    }
    else if (digits[0] == '0') { value_.printf("+%s%s", cc_digits, digits + 1); }
    else
    {
        value_.printf("+%s%s", cc_digits, digits);
    }
    return valid();
}

} // namespace gsm
