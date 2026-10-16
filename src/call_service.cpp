#include "gsm/voice/call_service.hpp"
#include "gsm/codec/ucs2.hpp"
#include "gsm/log/log.hpp"
#include "gsm/util/static_string.hpp"
#include <cstdio>
#include <cstring>

namespace gsm
{

static const log::Channel kLog{"call", log::Level::Debug};

const char* to_string(CallState s)
{
    switch (s)
    {
    case CallState::Idle:
        return "idle";
    case CallState::Dialing:
        return "dialing";
    case CallState::Outgoing:
        return "outgoing";
    case CallState::Incoming:
        return "incoming";
    case CallState::Active:
        return "active";
    case CallState::Releasing:
        return "releasing";
    }
    return "?";
}

const char* to_string(CallStep s)
{
    switch (s)
    {
    case CallStep::Idle:
        return "idle";
    case CallStep::EnableClip:
        return "clip";
    case CallStep::Dial:
        return "atd";
    case CallStep::Answer:
        return "ata";
    case CallStep::Hangup:
        return "hangup";
    case CallStep::HangupFallback:
        return "hangup-fb";
    case CallStep::ClccProbe:
        return "clcc";
    case CallStep::Done:
        return "done";
    }
    return "?";
}

const char* to_string(CallEndReason r)
{
    switch (r)
    {
    case CallEndReason::None:
        return "none";
    case CallEndReason::LocalHangup:
        return "local-hangup";
    case CallEndReason::RemoteHangup:
        return "remote-hangup";
    case CallEndReason::Busy:
        return "busy";
    case CallEndReason::NoAnswer:
        return "no-answer";
    case CallEndReason::NoCarrier:
        return "no-carrier";
    case CallEndReason::NoDialtone:
        return "no-dialtone";
    case CallEndReason::Timeout:
        return "timeout";
    case CallEndReason::Error:
        return "error";
    }
    return "?";
}

void CallService::set_state(CallState s)
{
    if (state_ == s) return;
    state_          = s;
    state_since_ms_ = at_.now();
    if (s == CallState::Active) active_since_ms_ = at_.now();
    if (s == CallState::Idle)
    {
        clcc_misses_      = 0;
        clcc_seen_before_ = false;
    }
    kLog.info("state=%s peer=%s", to_string(s), peer_.e164());
    if (on_state_) on_state_(s, peer_.e164());
}

uint32_t CallService::duration_ms(uint32_t now_ms) const
{
    if (state_ != CallState::Active || active_since_ms_ == 0) return 0;
    return now_ms - active_since_ms_;
}

bool CallService::start_dial(const char* number)
{
    if (active() || state_ != CallState::Idle) return false;
    if (!peer_.assign(number)) return false;
    result_           = AtResult{};
    end_reason_       = CallEndReason::None;
    fallback_         = false;
    clcc_seen_before_ = false;
    dial_since_ms_    = at_.now();
    step_             = (auto_clip_ && !clip_enabled_) ? CallStep::EnableClip : CallStep::Dial;
    issued_           = false;
    set_state(CallState::Dialing);
    return true;
}

bool CallService::start_answer()
{
    if (active() || state_ != CallState::Incoming) return false;
    result_   = AtResult{};
    fallback_ = false;
    step_     = CallStep::Answer;
    issued_   = false;
    return true;
}

bool CallService::start_hangup()
{
    if (active() && step_ != CallStep::Hangup) at_.abort();
    result_   = AtResult{};
    fallback_ = false;
    issue_hangup();
    return true;
}

void CallService::issue_hangup()
{
    step_   = CallStep::Hangup;
    issued_ = false;
    set_state(CallState::Releasing);
}

void CallService::cancel()
{
    if (state_ == CallState::Idle)
    {
        step_ = CallStep::Idle;
        return;
    }
    start_hangup();
}

void CallService::end_call(CallEndReason reason)
{
    end_reason_ = reason;
    peer_.clear();
    step_             = CallStep::Idle;
    issued_           = false;
    probe_            = false;
    clcc_misses_      = 0;
    clcc_seen_before_ = false;
    set_state(CallState::Idle);
    kLog.info("ended: %s", to_string(reason));
    if (on_ended_) on_ended_(reason);
}

void CallService::pump()
{
    if (!active() || issued_) return;
    char cmd[GSM_CMD_MAX];
    switch (step_)
    {
    case CallStep::EnableClip:
        issued_ = at_.execute("AT+CLIP=1", 1000, this);
        return;
    case CallStep::Dial:
        std::snprintf(cmd, sizeof(cmd), "ATD%s;", peer_.e164());
        dial_since_ms_ = at_.now();
        issued_        = at_.execute(cmd, 20000, this);
        return;
    case CallStep::Answer:
        issued_ = at_.execute("ATA", 20000, this);
        return;
    case CallStep::Hangup:
        issued_ = at_.execute(traits_.hangup_cmd, 5000, this);
        return;
    case CallStep::HangupFallback:
        issued_ = at_.execute("ATH", 5000, this);
        return;
    default:
        return;
    }
}

void CallService::monitor(uint32_t now_ms, bool engine_idle)
{
    if (state_ == CallState::Idle)
    {
        clcc_misses_ = 0;
        probe_       = false;
        return;
    }

    // --- automatic hangup / reject timers ---------------------------------
    if (policy_.auto_hangup)
    {
        const bool mo_pending = (state_ == CallState::Dialing || state_ == CallState::Outgoing);
        if (mo_pending && dial_since_ms_ &&
            hal::elapsed(now_ms, dial_since_ms_, policy_.dial_timeout_ms))
        {
            kLog.warn("no answer within %lu ms -> auto hangup",
                      static_cast<unsigned long>(policy_.dial_timeout_ms));
            end_reason_ = CallEndReason::Timeout;
            start_hangup();
            return;
        }
        if (state_ == CallState::Incoming && ring_since_ms_ &&
            hal::elapsed(now_ms, ring_since_ms_, policy_.ring_timeout_ms))
        {
            kLog.warn("ringing %lu ms unanswered -> auto reject",
                      static_cast<unsigned long>(policy_.ring_timeout_ms));
            end_reason_ = CallEndReason::Timeout;
            start_hangup();
            return;
        }
    }

    // --- +CLCC polling safety net -----------------------------------------
    if (!traits_.supports_clcc || !engine_idle || !policy_.clcc_poll_ms) return;
    if (step_ != CallStep::Idle || probe_) return;
    if (!hal::elapsed(now_ms, last_clcc_ms_, policy_.clcc_poll_ms)) return;

    last_clcc_ms_ = at_.now();
    clcc_seen_    = false;
    probe_        = true;
    issued_       = at_.execute("AT+CLCC", 2000, this);
    if (!issued_) probe_ = false;
}

void CallService::on_line(const char* line, uint16_t len)
{
    (void)len;
    if (!str_starts_with(line, "+CLCC:")) return;

    clcc_seen_        = true;
    clcc_seen_before_ = true;
    clcc_misses_      = 0;
    const char* tail  = std::strchr(line, ':');
    if (!tail) return;
    ++tail;

    char    f[8];
    int32_t stat = -1;
    if (csv_field(tail, 2, f, sizeof(f))) parse_int(f, stat);

    char raw[GSM_SMS_NUMBER_BYTES * 4 + 8];
    if (csv_field(tail, 5, raw, sizeof(raw)) && *raw && !peer_.valid())
    {
        if (codec::looks_like_ucs2_hex(raw))
        {
            char utf8[GSM_SMS_NUMBER_BYTES];
            codec::ucs2_hex_to_utf8(raw, utf8, sizeof(utf8));
            peer_.assign(utf8);
        }
        else
        {
            peer_.assign(raw);
        }
    }

    switch (stat)
    {
    case 0: // active
        if (state_ != CallState::Active)
        {
            kLog.info("CLCC: call active (answered)");
            set_state(CallState::Active);
        }
        break;
    case 2: // MO dialing
    case 3: // MO alerting
        if (state_ == CallState::Idle) set_state(CallState::Outgoing);
        break;
    case 4: // MT incoming
    case 5: // MT waiting
        if (state_ == CallState::Idle)
        {
            set_state(CallState::Incoming);
            ring_since_ms_ = at_.now();
        }
        break;
    default:
        break;
    }
}

void CallService::finish(AtStatus st, int16_t code)
{
    result_ = AtResult{st, code};
    step_   = CallStep::Idle;
    issued_ = false;
}

void CallService::on_final(const AtResult& res)
{
    issued_ = false;

    // +CLCC probe completion (supervision, not part of a job)
    if (probe_)
    {
        probe_ = false;
        if (state_ == CallState::Idle) return;
        if (clcc_seen_)
        {
            clcc_misses_ = 0;
            return;
        }

        // An empty call list is only trusted once the module had a fair chance
        // to show the call (it was listed before, the call is active, or the
        // state is old enough / the module marks CLCC authoritative).
        const bool trusted = clcc_seen_before_ || state_ == CallState::Active ||
                             traits_.clcc_authoritative ||
                             hal::elapsed(at_.now(), state_since_ms_, 2 * policy_.clcc_poll_ms);
        if (trusted && ++clcc_misses_ >= GSM_CALL_CLCC_MISSES)
        {
            // Call is gone but no end-URC reached us (typical M08R failure):
            // remember the reason and actively release the line.
            end_reason_ = (state_ == CallState::Incoming) ? CallEndReason::RemoteHangup
                          : (state_ == CallState::Active) ? CallEndReason::RemoteHangup
                                                          : CallEndReason::NoAnswer;
            kLog.warn("call vanished without URC (%s), releasing line", to_string(end_reason_));
            start_hangup();
        }
        return;
    }

    switch (step_)
    {
    case CallStep::EnableClip:
        clip_enabled_ = res.ok();
        step_         = CallStep::Dial;
        return;
    case CallStep::Dial:
        if (res.ok())
        {
            set_state(CallState::Outgoing);
            dial_since_ms_ = at_.now();
            finish(AtStatus::Ok, -1);
        }
        else
        {
            CallEndReason reason = CallEndReason::Error;
            switch (res.status)
            {
            case AtStatus::Busy:
                reason = CallEndReason::Busy;
                break;
            case AtStatus::NoAnswer:
                reason = CallEndReason::NoAnswer;
                break;
            case AtStatus::NoCarrier:
                reason = CallEndReason::NoCarrier;
                break;
            case AtStatus::NoDialtone:
                reason = CallEndReason::NoDialtone;
                break;
            case AtStatus::Timeout:
                reason = CallEndReason::Timeout;
                break;
            default:
                break;
            }
            end_call(reason);
            finish(res.status, res.code);
        }
        return;
    case CallStep::Answer:
        if (res.ok())
        {
            set_state(CallState::Active);
            finish(AtStatus::Ok, -1);
        }
        else
        {
            end_call(CallEndReason::Error);
            finish(res.status, res.code);
        }
        return;
    case CallStep::Hangup:
        if (!res.ok() && !fallback_)
        {
            // Primary hangup command failed: try the classic ATH once.
            fallback_ = true;
            step_     = CallStep::HangupFallback;
            return;
        }
        end_call(end_reason_ == CallEndReason::None ? CallEndReason::LocalHangup : end_reason_);
        finish(res.status, res.code);
        return;
    case CallStep::HangupFallback:
        end_call(end_reason_ == CallEndReason::None ? CallEndReason::LocalHangup : end_reason_);
        finish(res.status, res.code);
        return;
    default:
        finish(res.status, res.code);
        return;
    }
}

void CallService::notify_ring(const char* caller)
{
    if (caller && *caller) peer_.assign(caller);
    if (state_ == CallState::Idle)
    {
        ring_since_ms_ = at_.now();
        set_state(CallState::Incoming);
    }
    else if (state_ == CallState::Incoming)
    {
        ring_since_ms_ = at_.now(); // refresh on each ring burst
    }
}

void CallService::notify_hangup()
{
    if (state_ == CallState::Idle) return;
    const CallEndReason reason = (state_ == CallState::Dialing || state_ == CallState::Outgoing)
                                     ? CallEndReason::NoAnswer
                                     : CallEndReason::RemoteHangup;
    end_call(reason);
}

void CallService::notify_busy()
{
    if (state_ == CallState::Idle) return;
    end_call(CallEndReason::Busy);
}

} // namespace gsm
