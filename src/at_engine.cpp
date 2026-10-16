#include "gsm/core/at_engine.hpp"
#include <cstring>

namespace gsm
{

static const log::Channel kLog{"at", log::Level::Debug};

bool AtEngine::execute(const AtRequest& req, IAtObserver* observer)
{
    if (busy()) return false;
    if (!hal::elapsed(clock_.millis(), last_done_, GSM_AT_GUARD_TIME)) return false;

    observer_      = observer;
    expect_prompt_ = req.expect_prompt;
    timeout_       = req.timeout_ms;
    started_       = clock_.millis();
    sent_.clear();
    // Do NOT reset the line reader here. A PWRKEY cycle can leave
    // "NORMAL POWER DOWN" (or RDY) mid-line; wiping it makes the URC either
    // vanish or reassemble later, after AT has already succeeded.

    if (req.command)
    {
        sent_.assign(req.command);
        uart_.write(reinterpret_cast<const uint8_t*>(sent_.c_str()), sent_.size());
        const uint8_t crlf[2] = {'\r', '\n'};
        uart_.write(crlf, 2);
        if (trace_) kLog.debug("TX %s", sent_.c_str());
    }
    return true;
}

bool AtEngine::execute(const char* cmd, uint32_t timeout_ms, IAtObserver* observer)
{
    AtRequest req;
    req.command    = cmd;
    req.timeout_ms = timeout_ms;
    return execute(req, observer);
}

bool AtEngine::await_final(uint32_t timeout_ms, IAtObserver* observer)
{
    if (busy()) return false;
    observer_      = observer;
    expect_prompt_ = false;
    timeout_       = timeout_ms;
    started_       = clock_.millis();
    sent_.clear();
    return true;
}

uint16_t AtEngine::write_raw(const uint8_t* data, uint16_t len)
{
    if (trace_) kLog.bytes(log::Level::Trace, "TX< ", data, len);
    return uart_.write(data, len);
}

uint16_t AtEngine::write_raw(const char* text)
{
    return text ? write_raw(reinterpret_cast<const uint8_t*>(text),
                            static_cast<uint16_t>(std::strlen(text)))
                : 0;
}

void AtEngine::abort()
{
    if (!busy()) return;
    if (expect_prompt_) write_escape();
    finish(AtStatus::Aborted, -1);
}

void AtEngine::discard_rx()
{
    uart_.drain_rx();
    reader_.reset();
}

bool AtEngine::classify_final(const char* line, AtStatus& status, int16_t& code) const
{
    code = -1;
    if (std::strcmp(line, "OK") == 0)
    {
        status = AtStatus::Ok;
        return true;
    }
    if (std::strcmp(line, "ERROR") == 0)
    {
        status = AtStatus::Error;
        return true;
    }
    if (str_starts_with(line, "+CME ERROR:"))
    {
        int32_t v = -1;
        parse_int(line + 11, v);
        code   = static_cast<int16_t>(v);
        status = AtStatus::CmeError;
        return true;
    }
    if (str_starts_with(line, "+CMS ERROR:"))
    {
        int32_t v = -1;
        parse_int(line + 11, v);
        code   = static_cast<int16_t>(v);
        status = AtStatus::CmsError;
        return true;
    }
    if (std::strcmp(line, "NO CARRIER") == 0)
    {
        status = AtStatus::NoCarrier;
        return true;
    }
    if (std::strcmp(line, "BUSY") == 0)
    {
        status = AtStatus::Busy;
        return true;
    }
    if (std::strcmp(line, "NO ANSWER") == 0)
    {
        status = AtStatus::NoAnswer;
        return true;
    }
    if (std::strcmp(line, "NO DIALTONE") == 0)
    {
        status = AtStatus::NoDialtone;
        return true;
    }
    return false;
}

void AtEngine::finish(AtStatus status, int16_t code)
{
    IAtObserver* obs = observer_;
    observer_        = nullptr;
    expect_prompt_   = false;
    last_done_       = clock_.millis();
    if (obs)
    {
        AtResult res{status, code};
        obs->on_final(res);
    }
}

void AtEngine::tick()
{
    uint8_t byte = 0;
    while (uart_.read(&byte, 1) == 1)
    {
        const Token tok = reader_.feed(byte);
        if (tok == Token::None) continue;
        if (tok == Token::Overflow)
        {
            kLog.warn("line overflow, dropped");
            reader_.reset();
            continue;
        }
        if (tok == Token::Prompt)
        {
            reader_.reset();
            if (trace_) kLog.trace("RX prompt >");
            if (observer_)
            {
                observer_->on_prompt();
                if (expect_prompt_) finish(AtStatus::Ok, -1);
            }
            continue;
        }

        const char*    line = reader_.line();
        const uint16_t len  = reader_.length();
        if (trace_) kLog.trace("RX %s", line);

        if (!sent_.empty() && std::strcmp(line, sent_.c_str()) == 0)
        {
            reader_.reset();
            continue;
        }

        AtStatus status = AtStatus::Pending;
        int16_t  code   = -1;
        if (observer_ && classify_final(line, status, code))
        {
            reader_.reset();
            if (status == AtStatus::CmsError)
                kLog.warn("CMS %d: %s", code, cms_error_hint(code));
            else if (status == AtStatus::CmeError)
                kLog.warn("CME %d: %s", code, cme_error_hint(code));
            finish(status, code);
            continue;
        }

        if (urc_.dispatch(line, len))
        {
            reader_.reset();
            continue;
        }
        if (observer_) observer_->on_line(line, len);
        reader_.reset();
    }

    if (observer_ && timeout_ && hal::elapsed(clock_.millis(), started_, timeout_))
    {
        kLog.warn("timeout after %lums: %s", static_cast<unsigned long>(timeout_),
                  sent_.empty() ? "(payload phase)" : sent_.c_str());
        if (expect_prompt_) write_escape();
        finish(AtStatus::Timeout, -1);
    }
}

} // namespace gsm
