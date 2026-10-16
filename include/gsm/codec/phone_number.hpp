#pragma once
#include "gsm/gsm_config.hpp"
#include "gsm/util/static_string.hpp"

namespace gsm
{

class PhoneNumber
{
  public:
    PhoneNumber() = default;
    explicit PhoneNumber(const char* raw, const char* country_code = "+98")
    { assign(raw, country_code); }
    bool        assign(const char* raw, const char* country_code = "+98");
    const char* e164() const { return value_.c_str(); }
    bool        valid() const { return value_.size() >= 8; }
    void        clear() { value_.clear(); }

  private:
    StaticString<GSM_SMS_NUMBER_BYTES> value_;
};

} // namespace gsm
