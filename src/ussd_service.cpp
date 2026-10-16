#include "gsm/ussd/ussd_service.hpp"
#include "gsm/codec/ucs2.hpp"
#include "gsm/log/log.hpp"
#include <cstdio>
#include <cstring>

namespace gsm
{

static const log::Channel kLog{"ussd", log::Level::Debug};

const char* to_string(UssdStep s)
{
    switch (s)
    {
    case UssdStep::Idle:
        return "idle";
    case UssdStep::SetUssdMode:
        return "cusd-enable";
    case UssdStep::SendQuery:
        return "cusd-send";
    case UssdStep::SendReply:
        return "cusd-reply";
    case UssdStep::WaitUrc:
        return "wait-cusd-urc";
    case UssdStep::SessionOpen:
        return "session-open";
    case UssdStep::EndSession:
        return "cusd-end";
    case UssdStep::Done:
        return "done";
    }
    return "?";
}

// Encode outgoing text for AT+CUSD: ASCII -> raw (dcs=15), else -> UCS2 hex (dcs=72)
static bool encode_ussd_text(const char* text, char* out, uint16_t out_cap, uint8_t& dcs)
{
    if (!text || !*text || out_cap == 0) return false;

    // USSD codes always go as UCS2 hex (dcs=15 or 72 both work, but payload = UCS2 hex)
    if (codec::utf8_to_ucs2_hex(text, out, out_cap) == 0) return false;
    dcs = 15; // ← با 15 هم کار میکنه چون payload UCS2 hex است
    return true;
}

// Build the full AT+CUSD=1,"...",<dcs> command, reject on encode failure or truncation
static bool build_cusd_command(const char* text, char* cmd, uint16_t cmd_cap)
{
    char    payload[GSM_USSD_RESPONSE_BYTES * 4 + 8];
    uint8_t dcs = 15;
    if (!encode_ussd_text(text, payload, sizeof(payload), dcs)) return false;

    const int n = std::snprintf(cmd, cmd_cap, "AT+CUSD=1,\"%s\",%u", payload, dcs);
    return n > 0 && static_cast<uint16_t>(n) < cmd_cap;
}

// Decode incoming USSD text: trust reported dcs first, fall back to hex sniffing
template <typename StringT> static void decode_ussd_text(const char* raw, int32_t dcs, StringT& out)
{
    bool ucs2;
    if (dcs == 72)
        ucs2 = true;
    else if (dcs == 15)
        ucs2 = false;
    else
        ucs2 = codec::looks_like_ucs2_hex(raw); // unknown dcs -> sniff

    // safety net: some modems report a wrong dcs but the payload is clearly UCS2 hex
    if (!ucs2 && codec::looks_like_ucs2_hex(raw)) ucs2 = true;

    if (ucs2)
    {
        char utf8[GSM_USSD_RESPONSE_BYTES];
        codec::ucs2_hex_to_utf8(raw, utf8, sizeof(utf8));
        out.assign(utf8);
    }
    else
    {
        out.assign(raw);
    }
}

const char* UssdService::default_balance_code(Operator op)
{
    switch (op)
    {
    case Operator::Irancell:
        return "*141*1#";
    case Operator::Mci:
        return "*140*11#";
    case Operator::Rightel:
        return "*140#";
    default:
        return "*140#";
    }
}

bool UssdService::start_query(const char* ussd_code)
{
    if (active() || !ussd_code || !*ussd_code) return false;
    code_.assign(ussd_code);
    report_       = UssdResult{};
    session_open_ = false;
    goto_step(UssdStep::SetUssdMode);
    kLog.info("query: %s", code_.c_str());
    return true;
}

bool UssdService::start_balance_query(Operator op)
{ return start_query(default_balance_code(op)); }

// Continue an already-open interactive USSD session (network asked for more input)
bool UssdService::send_reply(const char* text)
{
    if (!session_open_ || issued_ || !text || !*text) return false;
    code_.assign(text);
    report_.response.assign("");
    goto_step(UssdStep::SendReply);
    kLog.info("reply: %s", code_.c_str());
    return true;
}

// Politely close an open interactive session (AT+CUSD=2)
bool UssdService::end_session()
{
    if (!session_open_ || issued_) return false;
    session_open_ = false;
    goto_step(UssdStep::EndSession);
    kLog.info("closing ussd session");
    return true;
}

bool UssdService::session_open() const
{ return session_open_; }

void UssdService::cancel()
{
    if (!active()) return;
    //! session will be close for this version of code
    at_.write_raw("AT+CUSD=2");

    at_.abort();
    complete(AtStatus::Aborted, -1);
}

void UssdService::complete(AtStatus st, int16_t code)
{
    report_.result = AtResult{st, code};
    step_          = UssdStep::Idle;
    issued_        = false;
    session_open_  = false;
    if (on_done_) on_done_(report_);
}

void UssdService::pump()
{
    if (!active() || issued_) return;
    char cmd[GSM_CMD_MAX];

    switch (step_)
    {
    case UssdStep::SetUssdMode:
        // AT+CUSD=1 enables presentation of USSD responses
        issued_ = at_.execute("AT+CUSD=2", 1000, this);
        issued_ = at_.execute("AT+CSCS=\"UCS2\"", 1000, this);
        return;

    case UssdStep::SendQuery:
    case UssdStep::SendReply:
        if (!build_cusd_command(code_.c_str(), cmd, sizeof(cmd)))
        {
            kLog.warn("ussd text encode failed");
            complete(AtStatus::Aborted, -2);
            return;
        }
        issued_ = at_.execute(cmd, 10000, this);
        return;

    case UssdStep::WaitUrc:
        // Wait for the async +CUSD URC from the cellular network
        if (hal::elapsed(at_.now(), wait_start_, GSM_AT_USSD_TIMEOUT))
        {
            kLog.warn("ussd network timeout");
            //! session will be close for this version of code
            at_.write_raw("AT+CUSD=2");

            complete(AtStatus::Timeout, -1);
        }
        return;

    case UssdStep::EndSession:
        // AT+CUSD=2 asks the modem to terminate the open session
        issued_ = at_.execute("AT+CUSD=2", 5000, this);
        return;

    default:
        return;
    }
}

void UssdService::on_line(const char* line, uint16_t len)
{
    (void)len;
    if (str_starts_with(line, "+CUSD:")) { notify_cusd(line); }
}

void UssdService::on_final(const AtResult& res)
{
    issued_ = false;

    // Stray final result: the URC already settled this (completed or opened a
    // session) before the command's own OK/ERROR arrived. Ignore it.
    if (step_ == UssdStep::Idle || step_ == UssdStep::SessionOpen) return;

    if (!res.ok())
    {
        kLog.warn("step %s failed", to_string(step_));
        complete(res.status, res.code);
        return;
    }

    switch (step_)
    {
    case UssdStep::SetUssdMode:
        goto_step(UssdStep::SendQuery);
        return;

    case UssdStep::SendQuery:
    case UssdStep::SendReply:
        // No URC yet (otherwise step_ would already differ) -> wait for it
        step_       = UssdStep::WaitUrc;
        wait_start_ = at_.now();
        return;

    case UssdStep::EndSession:
        //! session will be close for this version of code
        at_.write_raw("AT+CUSD=2");
        complete(AtStatus::Ok, 0);
        return;

    default:
        return;
    }
}

void UssdService::notify_cusd(const char* line)
{
    // Format: +CUSD: <m>,"<str>",<dcs>
    const char* tail = std::strchr(line, ':');
    if (!tail) return;
    ++tail;

    int32_t m = -1;
    char    m_buf[8];
    if (csv_field(tail, 0, m_buf, sizeof(m_buf))) parse_int(m_buf, m);

    int32_t dcs = -1;
    char    dcs_buf[8];
    if (csv_field(tail, 2, dcs_buf, sizeof(dcs_buf))) parse_int(dcs_buf, dcs);

    char raw[GSM_USSD_RESPONSE_BYTES * 4 + 8];
    if (csv_field(tail, 1, raw, sizeof(raw))) { decode_ussd_text(raw, dcs, report_.response); }
    else
    {
        report_.response.assign("");
    }

    report_.status_code = static_cast<int16_t>(m);
    kLog.info("cusd m=%d response: %s", static_cast<int>(m), report_.response.c_str());

    // Ignore unsolicited +CUSD while we are not actually waiting for one
    if (step_ != UssdStep::SendQuery && step_ != UssdStep::SendReply && step_ != UssdStep::WaitUrc)
        return;

    if (m == 1)
    {
        //! session will be close for this version of code
        at_.write_raw("AT+CUSD=2");
        complete(AtStatus::Ok, 0);
        return;

        // Network expects further user input: keep the session open for send_reply()
        // session_open_ = true;
        // goto_step(UssdStep::SessionOpen);
        // if (on_done_) on_done_(report_);
        // return;
    }

    // 0 = success, 2 = terminated by network, others reported as-is via status_code
    //! session will be close for this version of code
    at_.write_raw("AT+CUSD=2");
    complete(AtStatus::Ok, static_cast<int16_t>(m));
}

} // namespace gsm