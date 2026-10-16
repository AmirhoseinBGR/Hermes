#pragma once
#include "gsm/codec/phone_number.hpp"
#include "gsm/core/service.hpp"
#include "gsm/device/device_traits.hpp"
#include "gsm/gsm_config.hpp"
#include "gsm/util/delegate.hpp"

namespace gsm
{

enum class CallState : uint8_t
{
    Idle = 0,
    Dialing,
    Outgoing,
    Incoming,
    Active,
    Releasing
};

// Why a call went back to Idle (last ended call only).
enum class CallEndReason : uint8_t
{
    None = 0,
    LocalHangup,
    RemoteHangup,
    Busy,
    NoAnswer,
    NoCarrier,
    NoDialtone,
    Timeout,
    Error
};

enum class CallStep : uint8_t
{
    Idle = 0,
    EnableClip,
    Dial,
    Answer,
    Hangup,
    HangupFallback,
    ClccProbe,
    Done
};

const char* to_string(CallState s);
const char* to_string(CallStep s);
const char* to_string(CallEndReason r);

// Runtime-adjustable supervision rules (all defaults come from gsm_config.hpp).
struct CallPolicy
{
    // Auto-hangup an outgoing call that is not established within this window.
    uint32_t dial_timeout_ms = GSM_CALL_ANSWER_TIMEOUT_MS;
    // Auto-reject an incoming call that is not answered within this window.
    uint32_t ring_timeout_ms = GSM_CALL_RING_TIMEOUT_MS;
    // +CLCC call-state polling interval (also the safety net for missed URCs).
    uint32_t clcc_poll_ms = GSM_CALL_CLCC_POLL_MS;
    // Master switch for the automatic hangup / reject timers.
    bool auto_hangup = true;
};

class CallService final : public IService
{
  public:
    using StateChanged = Delegate<CallState, const char*>;
    using Ended        = Delegate<CallEndReason>;

    CallService(AtEngine& engine, SessionState& session, const DeviceTraits& traits)
        : at_(engine), session_(session), traits_(traits)
    {
    }

    bool start_dial(const char* number_e164);
    bool start_answer();
    bool start_hangup();

    // Self-driving supervision: timers, auto-hangup and +CLCC polling. Called on
    // every Modem tick even when no call job is the current job, so an incoming
    // call that lands between SMS jobs can never wedge the queue.
    void monitor(uint32_t now_ms, bool engine_idle);

    bool            active() const override { return step_ != CallStep::Idle; }
    void            pump() override;
    void            cancel() override;
    const char*     step_name() const override { return to_string(step_); }
    const AtResult& result() const override { return result_; }
    void            on_line(const char* line, uint16_t len) override;
    void            on_final(const AtResult& res) override;

    void notify_ring(const char* caller);
    void notify_hangup();
    void notify_busy();

    CallState   state() const { return state_; }
    const char* peer() const { return peer_.e164(); }
    bool        in_call() const { return state_ == CallState::Active; }
    bool        ringing() const { return state_ == CallState::Incoming; }
    // Any non-idle call activity: the job queue gate holds non-call jobs while set.
    bool          engaged() const { return state_ != CallState::Idle; }
    CallEndReason last_end_reason() const { return end_reason_; }
    uint32_t      duration_ms(uint32_t now_ms) const;

    void set_on_state(StateChanged cb) { on_state_ = cb; }
    void set_on_ended(Ended cb) { on_ended_ = cb; }
    void set_auto_clip(bool on) { auto_clip_ = on; }

    CallPolicy&       policy() { return policy_; }
    const CallPolicy& policy() const { return policy_; }

  private:
    void set_state(CallState s);
    void finish(AtStatus st, int16_t code);
    void end_call(CallEndReason reason);
    void issue_hangup();

    AtEngine&           at_;
    SessionState&       session_;
    const DeviceTraits& traits_;

    CallStep step_         = CallStep::Idle;
    bool     issued_       = false;
    bool     probe_        = false; // +CLCC probe in flight (not a job step)
    bool     clip_enabled_ = false;
    bool     auto_clip_    = true;
    bool     fallback_     = false; // ATH fallback already tried for this hangup

    CallState     state_      = CallState::Idle;
    CallEndReason end_reason_ = CallEndReason::None;
    PhoneNumber   peer_;
    AtResult      result_;
    StateChanged  on_state_;
    Ended         on_ended_;
    CallPolicy    policy_;

    // supervision timers
    uint32_t dial_since_ms_    = 0;
    uint32_t ring_since_ms_    = 0;
    uint32_t active_since_ms_  = 0;
    uint32_t state_since_ms_   = 0;
    uint32_t last_clcc_ms_     = 0;
    uint8_t  clcc_misses_      = 0;
    bool     clcc_seen_        = false; // +CLCC line seen for the current probe
    bool     clcc_seen_before_ = false; // call has appeared in +CLCC at least once
};

} // namespace gsm
