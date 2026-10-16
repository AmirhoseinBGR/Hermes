#include "gsm/sms/sms_service.hpp"
#include "gsm/codec/ucs2.hpp"
#include "gsm/log/log.hpp"
#include <cstring>

namespace gsm
{

static const log::Channel kLog{"sms", log::Level::Debug};

const char* to_string(SmsStep s)
{
    switch (s)
    {
    case SmsStep::Idle:
        return "idle";
    case SmsStep::SetTextMode:
        return "cmgf";
    case SmsStep::SetCharset:
        return "cscs";
    case SmsStep::SetCsmp:
        return "csmp";
    case SmsStep::SetStorage:
        return "cpms";
    case SmsStep::SendHeader:
        return "cmgs-header";
    case SmsStep::WritePayload:
        return "payload";
    case SmsStep::WaitAck:
        return "wait-cmgs";
    case SmsStep::ListUnread:
        return "cmgl";
    case SmsStep::ReadMessage:
        return "cmgr";
    case SmsStep::DeleteIndex:
        return "cmgd";
    case SmsStep::DeleteRead:
        return "cmgd-read";
    case SmsStep::Done:
        return "done";
    }
    return "?";
}

bool SmsService::wants_ucs2() const
{ return active_enc_ == SmsEncoding::Ucs2; }

// Number of UTF-8 code points (what a user perceives as characters).
uint16_t SmsService::count_chars(const char* utf8)
{
    if (!utf8) return 0;
    uint16_t n = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8); *p; ++p)
    {
        if ((*p & 0xC0) != 0x80) ++n; // count lead bytes only
    }
    return n;
}

bool SmsService::start_send(const char* number, const char* utf8_text, SmsEncoding enc)
{
    if (active()) return false;
    const uint16_t chars = count_chars(utf8_text);
    if (max_chars_ && chars > max_chars_)
    {
        kLog.error("text too long: %u chars > limit %u", chars, max_chars_);
        result_ = AtResult{AtStatus::Error, kErrSmsTooLong};
        return false;
    }
    if (!recipient_.assign(number))
    {
        kLog.error("bad recipient");
        return false;
    }
    outgoing_.assign(utf8_text);
    target_index_ = -1;
    active_enc_   = enc == SmsEncoding::Auto ? default_enc_ : enc;
    if (active_enc_ == SmsEncoding::Gsm7 && !codec::is_ascii_only(outgoing_.c_str()))
    {
        kLog.warn("non-ascii text, forcing UCS2");
        active_enc_ = SmsEncoding::Ucs2;
    }
    report_      = SmsSendReport{};
    result_      = AtResult{};
    prompt_seen_ = false;
    goto_step(SmsStep::SetTextMode);
    kLog.info("send -> %s (%u chars / %u units, %s)", recipient_.e164(), chars,
              codec::ucs2_units(outgoing_.c_str()), wants_ucs2() ? "UCS2" : "GSM7");
    return true;
}

bool SmsService::start_read(int16_t index, bool delete_after)
{
    if (active()) return false;
    target_index_ = index;
    recipient_.clear();
    delete_after_ = delete_after;
    active_enc_   = SmsEncoding::Ucs2;
    last_.clear();
    result_ = AtResult{};
    goto_step(SmsStep::SetTextMode);
    expect_body_ = false;
    return true;
}

bool SmsService::start_scan_unread()
{
    if (active()) return false;
    target_index_ = -1;
    recipient_.clear();
    pending_count_ = 0;
    active_enc_    = SmsEncoding::Ucs2;
    result_        = AtResult{};
    goto_step(SmsStep::SetTextMode);
    return true;
}

bool SmsService::start_delete_index(int16_t index)
{
    if (active()) return false;
    target_index_ = index;
    result_       = AtResult{};
    goto_step(SmsStep::DeleteIndex);
    return true;
}

bool SmsService::start_delete_read()
{
    if (active()) return false;
    result_ = AtResult{};
    goto_step(SmsStep::DeleteRead);
    return true;
}

void SmsService::queue_index(int16_t index)
{
    if (pending_count_ >= GSM_SMS_INBOX_SLOTS) return;
    pending_[pending_count_++] = index;
}

void SmsService::cancel()
{
    if (!active()) return;
    at_.abort();
    result_ = AtResult{AtStatus::Aborted, -1};
    step_   = SmsStep::Idle;
    kLog.warn("cancelled");
}

void SmsService::fail(AtStatus st, int16_t code)
{
    result_        = AtResult{st, code};
    report_.result = result_;
    step_          = SmsStep::Idle;
    session_.invalidate();
    kLog.error("failed at %s: %s(%d)", step_name(), to_string(st), code);
}

void SmsService::complete()
{
    result_        = AtResult{AtStatus::Ok, -1};
    report_.result = result_;
    step_          = SmsStep::Idle;
}

void SmsService::pump()
{
    for (uint8_t guard = 0; guard < 8; ++guard)
    {
        const SmsStep before = step_;
        advance_step();
        if (issued_ || !active() || step_ == before) return;
    }
}

void SmsService::advance_step()
{
    if (!active() || issued_) return;

    char cmd[GSM_CMD_MAX];
    switch (step_)
    {
    case SmsStep::SetTextMode:
        if (session_.text_mode)
        {
            goto_step(SmsStep::SetCharset);
            return;
        }
        issued_ = at_.execute("AT+CMGF=1", 1000, this);
        return;

    case SmsStep::SetCharset: {
        const Charset want = wants_ucs2() ? Charset::Ucs2 : Charset::Gsm;
        if (session_.charset == want)
        {
            goto_step(SmsStep::SetCsmp);
            return;
        }
        issued_ =
            at_.execute(want == Charset::Ucs2 ? "AT+CSCS=\"UCS2\"" : "AT+CSCS=\"GSM\"", 1000, this);
        return;
    }

    case SmsStep::SetCsmp: {
        const bool need_ucs2 = wants_ucs2();
        if (session_.csmp_ucs2 == need_ucs2 && session_.charset != Charset::Unknown)
        {
            goto_step(SmsStep::SetStorage);
            return;
        }
        issued_ = at_.execute(need_ucs2 ? traits_.csmp_ucs2 : traits_.csmp_gsm, 1000, this);
        return;
    }

    case SmsStep::SetStorage:
        if (session_.storage_ready || !traits_.storage_cmd)
        {
            if (target_index_ >= 0)
                goto_step(SmsStep::ReadMessage);
            else if (!recipient_.valid())
                goto_step(SmsStep::ListUnread);
            else
                goto_step(SmsStep::SendHeader);
            return;
        }
        issued_ = at_.execute(traits_.storage_cmd, 2000, this);
        return;

    case SmsStep::SendHeader: {
        char dest[GSM_SMS_NUMBER_BYTES * 4 + 8];
        if (wants_ucs2() && traits_.ucs2_recipient_required)
        {
            if (!codec::ascii_to_ucs2_hex(recipient_.e164(), dest, sizeof(dest)))
            {
                fail(AtStatus::Error, -1);
                return;
            }
        }
        else
        {
            std::snprintf(dest, sizeof(dest), "%s", recipient_.e164());
        }
        std::snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", dest);
        AtRequest req;
        req.command       = cmd;
        req.timeout_ms    = traits_.prompt_timeout_ms;
        req.expect_prompt = true;
        prompt_seen_      = false;
        issued_           = at_.execute(req, this);
        return;
    }

    case SmsStep::WritePayload: {
        if (wants_ucs2())
        {
            char hex[GSM_SMS_TEXT_BYTES * 4 + 4];
            if (!codec::utf8_to_ucs2_hex(outgoing_.c_str(), hex, sizeof(hex)))
            {
                at_.write_escape();
                fail(AtStatus::Error, -1);
                return;
            }
            at_.write_raw(hex);
        }
        else
        {
            at_.write_raw(outgoing_.c_str());
        }
        at_.write_ctrl_z();
        goto_step(SmsStep::WaitAck);
        issued_ = at_.await_final(traits_.send_timeout_ms, this);
        return;
    }

    case SmsStep::ListUnread:
        issued_ = at_.execute("AT+CMGL=\"REC UNREAD\"", 15000, this);
        return;

    case SmsStep::ReadMessage:
        std::snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", target_index_);
        expect_body_ = false;
        last_.clear();
        last_.storage_index = target_index_;
        issued_             = at_.execute(cmd, 5000, this);
        return;

    case SmsStep::DeleteIndex:
        std::snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", target_index_);
        issued_ = at_.execute(cmd, 5000, this);
        return;

    case SmsStep::DeleteRead:
        issued_ = at_.execute(traits_.supports_cmgd_delete_all ? "AT+CMGD=1,1" : "AT+CMGD=1", 10000,
                              this);
        return;

    default:
        return;
    }
}

void SmsService::on_prompt()
{
    prompt_seen_ = true;
    if (step_ == SmsStep::SendHeader)
    {
        step_   = SmsStep::WritePayload;
        issued_ = false;
    }
}

void SmsService::parse_cmgr_header(const char* line)
{
    const bool  is_list = str_starts_with(line, "+CMGL:");
    const char* tail    = std::strchr(line, ':');
    if (!tail) return;
    ++tail;

    uint8_t f_num  = is_list ? 2 : 1;
    uint8_t f_time = is_list ? 4 : 3;
    if (is_list)
    {
        int32_t idx = -1;
        if (parse_int(tail, idx)) last_.storage_index = static_cast<int16_t>(idx);
    }
    char raw[GSM_SMS_NUMBER_BYTES * 4 + 8];
    if (csv_field(tail, f_num, raw, sizeof(raw)))
    {
        if (codec::looks_like_ucs2_hex(raw))
        {
            char utf8[GSM_SMS_NUMBER_BYTES];
            codec::ucs2_hex_to_utf8(raw, utf8, sizeof(utf8));
            last_.number.assign(utf8);
        }
        else
        {
            last_.number.assign(raw);
        }
    }
    char ts[32];
    if (csv_field(tail, f_time, ts, sizeof(ts))) last_.timestamp.assign(ts);
    expect_body_ = true;
}

void SmsService::store_body(const char* line)
{
    if (codec::looks_like_ucs2_hex(line))
    {
        char utf8[GSM_SMS_TEXT_BYTES];
        codec::ucs2_hex_to_utf8(line, utf8, sizeof(utf8));
        last_.text.assign(utf8);
        last_.encoding = SmsEncoding::Ucs2;
    }
    else
    {
        last_.text.assign(line);
        last_.encoding = SmsEncoding::Gsm7;
    }
    expect_body_ = false;
}

void SmsService::on_line(const char* line, uint16_t len)
{
    (void)len;
    if (str_starts_with(line, "+CMGS:"))
    {
        int32_t mr = -1;
        parse_int(line + 6, mr);
        report_.reference = static_cast<int16_t>(mr);
        return;
    }
    if (str_starts_with(line, "+CMGR:") || str_starts_with(line, "+CMGL:"))
    {
        parse_cmgr_header(line);
        return;
    }
    if (str_starts_with(line, "+CPMS:")) return;
    if (expect_body_)
    {
        store_body(line);
        if (step_ == SmsStep::ListUnread)
        {
            queue_index(last_.storage_index);
            if (on_received_) on_received_(last_);
            last_.clear();
        }
    }
}

void SmsService::on_final(const AtResult& res)
{
    issued_ = false;
    if (res.status != AtStatus::Ok)
    {
        if (step_ == SmsStep::WritePayload || step_ == SmsStep::WaitAck) at_.write_escape();
        fail(res.status, res.code);
        return;
    }

    switch (step_)
    {
    case SmsStep::SetTextMode:
        session_.text_mode = true;
        goto_step(SmsStep::SetCharset);
        return;
    case SmsStep::SetCharset:
        session_.charset = wants_ucs2() ? Charset::Ucs2 : Charset::Gsm;
        goto_step(SmsStep::SetCsmp);
        return;
    case SmsStep::SetCsmp:
        session_.csmp_ucs2 = wants_ucs2();
        goto_step(SmsStep::SetStorage);
        return;
    case SmsStep::SetStorage:
        session_.storage_ready = true;
        goto_step(SmsStep::SetStorage);
        return;
    case SmsStep::SendHeader:
        if (!prompt_seen_) fail(AtStatus::Error, -1);
        return;
    case SmsStep::WritePayload:
        return;
    case SmsStep::WaitAck:
        kLog.info("sent, ref=%d", report_.reference);
        complete();
        return;
    case SmsStep::ListUnread:
        kLog.debug("scan done, %u unread", pending_count_);
        complete();
        return;
    case SmsStep::ReadMessage:
        if (last_.text.size() && on_received_) on_received_(last_);
        if (delete_after_ && last_.storage_index >= 0)
        {
            deleting_after_read_ = true;
            target_index_        = last_.storage_index;
            goto_step(SmsStep::DeleteIndex);
            return;
        }
        complete();
        return;
    case SmsStep::DeleteIndex:
        deleting_after_read_ = false;
        complete();
        return;
    case SmsStep::DeleteRead:
        complete();
        return;
    default:
        complete();
        return;
    }
}

} // namespace gsm
