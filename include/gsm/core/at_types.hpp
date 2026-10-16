#pragma once
#include <cstdint>

namespace gsm
{

enum class AtStatus : uint8_t
{
    Pending = 0,
    Ok,
    Error,
    CmeError,
    CmsError,
    Timeout,
    Aborted,
    NoDialtone,
    Busy,
    NoCarrier,
    NoAnswer,
};

struct AtResult
{
    AtStatus status = AtStatus::Pending;
    int16_t  code   = -1;
    bool     ok() const { return status == AtStatus::Ok; }
};

// Library-level error codes carried in AtResult::code (below -99).
constexpr int16_t kErrSmsTooLong  = -100; // outgoing text exceeds the strict length guard
constexpr int16_t kErrCallTimeout = -101; // call supervision auto-hangup fired
constexpr int16_t kErrJobStalled  = -102; // job could not be started after retries

const char* to_string(AtStatus s);
const char* cms_error_hint(int16_t code);
const char* cme_error_hint(int16_t code);

class IAtObserver
{
  public:
    virtual ~IAtObserver() = default;
    virtual void on_line(const char* line, uint16_t len)
    {
        (void)line;
        (void)len;
    }
    virtual void on_prompt() {}
    virtual void on_final(const AtResult& res) { (void)res; }
};

} // namespace gsm
