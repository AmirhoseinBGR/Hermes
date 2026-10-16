#include "gsm/modem/modem.hpp"
#include "gsm/log/log.hpp"
#include <cstdio>

namespace gsm
{

static const log::Channel kLog{"modem", log::Level::Info};

struct BootCmd
{
    const char* cmd;
    uint32_t    timeout_ms;
    bool        optional;
};
static const BootCmd kBoot[] = {
    {"ATE0", 1000, false},      {"AT+CMEE=2", 1000, true},
    {"AT+CLIP=1", 1000, true},  {"AT+CNMI=2,1,0,0,0", 1000, true},
    {"AT+CMGF=1", 1000, false}, {"AT+CSCS=\"UCS2\"", 1000, false},
    {nullptr, 0, false}, // traits_.csmp_ucs2
    {nullptr, 0, false}, // traits_.storage_cmd
};
static constexpr uint8_t kBootCount = sizeof(kBoot) / sizeof(kBoot[0]);

const char* to_string(ModemState s)
{
    switch (s)
    {
    case ModemState::Off:
        return "off";
    case ModemState::Booting:
        return "booting";
    case ModemState::PowerPulsing:
        return "pwrkey-pulsing";
    case ModemState::WaitingRegistration:
        return "waiting-reg";
    case ModemState::Configuring:
        return "configuring";
    case ModemState::Ready:
        return "ready";
    case ModemState::Working:
        return "working";
    case ModemState::Ringing:
        return "ringing";
    case ModemState::InCall:
        return "in-call";
    case ModemState::SimMissing:
        return "sim-missing";
    case ModemState::Fault:
        return "fault";
    }
    return "?";
}

namespace
{
hal::IClock* g_log_clock = nullptr;
uint32_t     log_now()
{ return g_log_clock ? g_log_clock->millis() : 0u; }
} // namespace

Modem::Modem(hal::IUart& uart, hal::IClock& clock, const DeviceTraits& traits,
             hal::IPowerControl* power)
    : uart_(uart), clock_(clock), traits_(traits), power_(power), at_(uart, clock, urc_),
      sms_(at_, session_, traits), call_(at_, session_, traits), stat_(at_, status_, traits),
      ussd_(at_, session_, traits)
{
    urc_.set_handler(UrcRouter::Handler(&Modem::urc_trampoline, this));
    watchdog_.bind_status(&status_);
}

void Modem::urc_trampoline(void* ctx, const UrcEvent& ev)
{ static_cast<Modem*>(ctx)->handle_urc(ev); }

void Modem::begin()
{
    g_log_clock = &clock_;
    log::Registry::set_clock(&log_now);
    session_.invalidate();
    at_probe_count_       = 0;
    pwrkey_pulse_count_   = 0;
    boot_index_           = 0;
    at_issued_            = false;
    consecutive_timeouts_ = 0;
    last_sim_probe_       = 0;
    last_cfun_cycle_      = 0;
    sim_recovery_phase_   = 0;
    start_failures_       = 0;
    if (pwrkey_asserted_) release_pwrkey(); // never leave the pin pulled
    pwrkey_pulse_ms_    = GSM_PWRKEY_PULSE_MS;
    boot_guard_active_ = false; // a plain (re)start probes as soon as it can
    watchdog_.on_reset_started(clock_.millis());
    enter(ModemState::Booting);
    kLog.info("begin (%s)", traits_.name);
}

void Modem::reset()
{
    at_.abort();
    stop_all(false);
    begin();
}

void Modem::power_cycle()
{
    if (!power_)
    {
        kLog.warn("Software Power-Cycle (no PWRKEY pin available)");
        reset();
        return;
    }
    kLog.warn("Hardware Power-Cycle: pulsing PWRKEY pin");
    reset();                         // abort, drop queue state, enter Booting
    pwrkey_pulse_count_ = 0;         // a manual cycle may pulse twice (off -> on)
    at_probe_count_     = 0;
    // tick() now drives: assert -> hold pwrkey_pulse_ms -> release -> boot guard.
    enter(ModemState::PowerPulsing);
}

void Modem::pulse_pwrkey(uint32_t pulse_ms)
{
    if (!power_)
    {
        kLog.warn("Cannot pulse PWRKEY: no IPowerControl provided!");
        return;
    }
    kLog.info("Manual PWRKEY pulse (%lu ms)", static_cast<unsigned long>(pulse_ms));
    reset();
    pwrkey_pulse_ms_    = pulse_ms ? pulse_ms : GSM_PWRKEY_PULSE_MS;
    pwrkey_pulse_count_ = 0;
    at_probe_count_     = 0;
    enter(ModemState::PowerPulsing);
}

void Modem::start_pwrkey_pulse()
{
    if (!power_) return;
    ++pwrkey_pulse_count_;
    kLog.warn("pulsing PWRKEY (#%u, hold %lums)", pwrkey_pulse_count_,
              static_cast<unsigned long>(pwrkey_pulse_ms_));
    power_->power_key(true);
    pwrkey_asserted_ = true;
    pwrkey_since_    = clock_.millis();
}

void Modem::release_pwrkey()
{
    if (power_) power_->power_key(false);
    pwrkey_asserted_   = false;
    boot_guard_active_ = true;
    boot_guard_since_  = clock_.millis();
    kLog.info("PWRKEY released, boot guard %lums",
              static_cast<unsigned long>(GSM_MODEM_BOOT_GUARD_MS));
}

bool Modem::boot_guard_elapsed() const
{
    // Only a real PWRKEY release arms the guard: a soft reset / software reboot
    // keeps probing the (still running) module immediately as before.
    return !boot_guard_active_ ||
           hal::elapsed(clock_.millis(), boot_guard_since_, GSM_MODEM_BOOT_GUARD_MS);
}

bool Modem::is_call_job(JobKind kind)
{ return kind == JobKind::CallDial || kind == JobKind::CallAnswer || kind == JobKind::CallHangup; }

bool Modem::call_gate() const
{ return call_.state() != CallState::Idle; }

void Modem::preempt_for_call()
{
    if (current_.state != JobState::Running || is_call_job(current_.kind)) return;
    IService* svc = service_for(current_.kind);
    if (svc) svc->cancel(); // ESC-safe abort of any pending SMS payload
    kLog.warn("call interrupt: deferring #%u %s", current_.id, to_string(current_.kind));
    current_.deferred = true;
    current_.state    = JobState::Queued;
}

void Modem::full_restart(bool hardware, bool keep_jobs)
{
    at_.abort();
    if (keep_jobs)
    {
        if (current_.state == JobState::Running)
        {
            IService* svc = service_for(current_.kind);
            if (svc) svc->cancel();
            AtResult res{AtStatus::Aborted, -1};
            finish_job(res);
        }
    }
    else
    {
        stop_all(false);
    }
    if (hardware && power_)
    {
        // Non-blocking: the FSM holds the pin and waits out the boot guard.
        kLog.warn("full hardware power-cycle: pulsing PWRKEY");
        begin(); // begin() already released a stuck PWRKEY and armed the guard
        pwrkey_pulse_count_ = 0;
        at_probe_count_     = 0;
        enter(ModemState::PowerPulsing);
        return;
    }
    begin();
}

void Modem::maintenance_service()
{
    watchdog_.tick(clock_.millis());
    if (!watchdog_.reset_pending())
    {
        maintenance_since_ = 0;
        return;
    }

    const uint32_t now = clock_.millis();
    if (maintenance_since_ == 0) maintenance_since_ = now;

    const bool safe =
        call_.state() == CallState::Idle && at_.idle() && current_.kind == JobKind::None;
    const bool forced = hal::elapsed(now, maintenance_since_, watchdog_.policy().grace_ms);
    if (!safe && !forced) return;

    const ResetReason reason = watchdog_.take_pending();
    kLog.warn("maintenance reset (%s%s)", to_string(reason), (forced && !safe) ? ", forced" : "");
    maintenance_since_ = 0;
    full_restart(watchdog_.policy().use_power_cycle && power_ != nullptr, /*keep_jobs*/ true);
}

void Modem::enter(ModemState s)
{
    if (state_ == s) return;
    // Safety: a PWRKEY pulse must never survive a state change (e.g. Fault),
    // otherwise the module would be left powered off with the pin held low.
    if (state_ == ModemState::PowerPulsing && pwrkey_asserted_) release_pwrkey();
    kLog.debug("%s -> %s", to_string(state_), to_string(s));
    state_       = s;
    state_since_ = clock_.millis();
    at_issued_   = false;
}

const char* Modem::step() const
{
    switch (state_)
    {
    case ModemState::Booting:
        return boot_guard_elapsed() ? "probe-at" : "boot-guard";
    case ModemState::PowerPulsing:
        return pwrkey_asserted_ ? "pwrkey-hold" : "pwrkey";
    case ModemState::WaitingRegistration:
        return "creg-wait";
    case ModemState::Configuring:
        return "bootstrap";
    case ModemState::SimMissing:
        return sim_recovery_phase_ == 1 ? "cfun-0"
                                        : (sim_recovery_phase_ == 2 ? "cfun-1" : "sim-probe");
    default:
        break;
    }
    switch (current_.kind)
    {
    case JobKind::SmsSend:
    case JobKind::SmsRead:
    case JobKind::SmsScan:
    case JobKind::SmsDeleteIndex:
    case JobKind::SmsDeleteRead:
        return sms_.step_name();
    case JobKind::CallDial:
    case JobKind::CallAnswer:
    case JobKind::CallHangup:
        return call_.step_name();
    case JobKind::StatusSweep:
        return stat_.step_name();
    case JobKind::UssdQuery:
        return ussd_.step_name();
    default:
        return "idle";
    }
}

IService* Modem::service_for(JobKind kind)
{
    switch (kind)
    {
    case JobKind::SmsSend:
    case JobKind::SmsRead:
    case JobKind::SmsScan:
    case JobKind::SmsDeleteIndex:
    case JobKind::SmsDeleteRead:
        return &sms_;
    case JobKind::CallDial:
    case JobKind::CallAnswer:
    case JobKind::CallHangup:
        return &call_;
    case JobKind::StatusSweep:
        return &stat_;
    case JobKind::UssdQuery:
        return &ussd_;
    default:
        return nullptr;
    }
}

JobId Modem::enqueue(Job& job, JobCallback cb)
{
    job.id = next_id_++;
    if (next_id_ == 0) next_id_ = 1;
    job.enqueued_ms = clock_.millis();
    job.state       = JobState::Queued;
    if (!queue_.push(job, cb))
    {
        kLog.warn("queue full, dropped %s", to_string(job.kind));
        return kInvalidJob;
    }
    kLog.debug("queued #%u %s (depth=%u)", job.id, to_string(job.kind), queue_.size());
    return job.id;
}

JobId Modem::send_sms(const char* number, const char* utf8_text, JobCallback cb, SmsEncoding enc)
{
    Job job;
    job.kind = JobKind::SmsSend;
    job.number.assign(number);
    job.text.assign(utf8_text);
    job.encoding     = enc;
    job.max_attempts = 2;

    // Strict length guard: reject over-long texts up front (the M08R family is
    // known to hang / emit empty SMS on large payloads). Limit is adjustable.
    const uint16_t chars = SmsService::count_chars(utf8_text);
    const uint16_t limit = sms_.max_chars();
    if (limit && chars > limit)
    {
        kLog.error("SMS rejected: %u chars exceeds limit %u", chars, limit);
        job.state       = JobState::Failed;
        job.result      = AtResult{AtStatus::Error, kErrSmsTooLong};
        job.enqueued_ms = clock_.millis();
        cb(job);
        on_job_(job);
        return kInvalidJob;
    }
    return enqueue(job, cb);
}

JobId Modem::read_sms(int16_t index, JobCallback cb)
{
    Job job;
    job.kind  = JobKind::SmsRead;
    job.index = index;
    return enqueue(job, cb);
}

JobId Modem::scan_sms(JobCallback cb)
{
    Job job;
    job.kind = JobKind::SmsScan;
    return enqueue(job, cb);
}

JobId Modem::delete_read_sms(JobCallback cb)
{
    Job job;
    job.kind = JobKind::SmsDeleteRead;
    return enqueue(job, cb);
}

JobId Modem::dial(const char* number, JobCallback cb)
{
    Job job;
    job.kind = JobKind::CallDial;
    job.number.assign(number);
    return enqueue(job, cb);
}

JobId Modem::answer(JobCallback cb)
{
    Job job;
    job.kind   = JobKind::CallAnswer;
    job.urgent = true;
    return enqueue(job, cb);
}

JobId Modem::hang_up(JobCallback cb)
{
    Job job;
    job.kind   = JobKind::CallHangup;
    job.urgent = true;
    return enqueue(job, cb);
}

JobId Modem::refresh_status(JobCallback cb)
{
    Job job;
    job.kind = JobKind::StatusSweep;
    return enqueue(job, cb);
}

JobId Modem::ussd(const char* code, JobCallback cb)
{
    Job job;
    job.kind = JobKind::UssdQuery;
    job.text.assign(code);
    return enqueue(job, cb);
}

JobId Modem::check_balance(JobCallback cb)
{ return ussd(UssdService::default_balance_code(status_.op), cb); }

bool Modem::cancel(JobId id)
{
    if (current_.id == id && current_.state == JobState::Running)
    {
        IService* svc = service_for(current_.kind);
        if (svc) svc->cancel();
        AtResult res{AtStatus::Aborted, -1};
        finish_job(res);
        return true;
    }
    Job         job;
    JobCallback cb;
    if (!queue_.cancel(id, job, cb)) return false;
    cb(job);
    on_job_(job);
    return true;
}

void Modem::stop_all(bool also_pause)
{
    if (also_pause) queue_.pause();
    if (current_.state == JobState::Running)
    {
        IService* svc = service_for(current_.kind);
        if (svc) svc->cancel();
        AtResult res{AtStatus::Aborted, -1};
        finish_job(res);
    }
    queue_.clear();
    flush_cancelled();
    kLog.warn("stop_all (queue %s)", queue_.paused() ? "paused" : "open");
}

void Modem::flush_cancelled()
{
    Job         job;
    JobCallback cb;
    while (queue_.take_cancelled(job, cb))
    {
        cb(job);
        on_job_(job);
    }
}

bool Modem::start_job(Job& job)
{
    IService* svc = service_for(job.kind);
    if (!svc) return false;
    bool started = false;
    if (job.deferred)
        job.deferred = false; // call-preempted retry: keep the attempt budget
    else
        ++job.attempts;
    switch (job.kind)
    {
    case JobKind::SmsSend:
        started = sms_.start_send(job.number.c_str(), job.text.c_str(), job.encoding);
        break;
    case JobKind::SmsRead:
        started = sms_.start_read(job.index);
        break;
    case JobKind::SmsScan:
        started = sms_.start_scan_unread();
        break;
    case JobKind::SmsDeleteIndex:
        started = sms_.start_delete_index(job.index);
        break;
    case JobKind::SmsDeleteRead:
        started = sms_.start_delete_read();
        break;
    case JobKind::CallDial:
        started = call_.start_dial(job.number.c_str());
        break;
    case JobKind::CallAnswer:
        started = call_.start_answer();
        break;
    case JobKind::CallHangup:
        started = call_.start_hangup();
        break;
    case JobKind::StatusSweep:
        started = stat_.start_full_sweep();
        break;
    case JobKind::UssdQuery:
        started = ussd_.start_query(job.text.c_str());
        break;
    default:
        break;
    }
    if (!started) return false;
    job.state = JobState::Running;
    enter(ModemState::Working);
    kLog.info("run #%u %s (try %u/%u)", job.id, to_string(job.kind), job.attempts,
              job.max_attempts);
    return true;
}

void Modem::finish_job(const AtResult& res)
{
    current_.result = res;
    if (current_.kind == JobKind::SmsSend)
        current_.reference = sms_.send_report().reference;
    else if (current_.kind == JobKind::UssdQuery)
        current_.ussd_response = ussd_.last_result().response;

    if (res.status == AtStatus::Timeout)
    {
        ++consecutive_timeouts_;
        kLog.warn("Job timed out (consecutive timeouts: %u)", consecutive_timeouts_);
        if (consecutive_timeouts_ >= GSM_CONSECUTIVE_TIMEOUTS_RESET) { check_watchdog(); }
    }
    else if (res.ok()) { consecutive_timeouts_ = 0; }

    const bool policy_reject = (res.code == kErrSmsTooLong || res.code == kErrJobStalled);
    const bool retryable     = !res.ok() && res.status != AtStatus::Aborted && !policy_reject &&
                               current_.attempts < current_.max_attempts;
    if (retryable)
    {
        kLog.warn("#%u %s failed (%s), retrying", current_.id, to_string(current_.kind),
                  to_string(res.status));
        current_.state = JobState::Queued;
        return;
    }

    watchdog_.note_operation();

    current_.state =
        res.ok() ? JobState::Succeeded
                 : (res.status == AtStatus::Aborted ? JobState::Cancelled : JobState::Failed);
    kLog.info("#%u %s -> %s", current_.id, to_string(current_.kind), to_string(current_.state));
    current_cb_(current_);
    on_job_(current_);
    current_.clear();
    current_cb_.clear();
    if (call_.in_call())
        enter(ModemState::InCall);
    else if (call_.ringing())
        enter(ModemState::Ringing);
    else
        enter(ModemState::Ready);
}

void Modem::handle_urc(const UrcEvent& ev)
{
    switch (ev.kind)
    {
    case Urc::Ring:
        preempt_for_call();
        call_.notify_ring(nullptr);
        if (state_ == ModemState::Ready || state_ == ModemState::Working)
            enter(ModemState::Ringing);
        break;
    case Urc::CallerId: {
        char num[GSM_SMS_NUMBER_BYTES];
        csv_field(ev.line + 6, 0, num, sizeof(num));
        preempt_for_call();
        call_.notify_ring(num);
        if (state_ == ModemState::Ready || state_ == ModemState::Working)
            enter(ModemState::Ringing);
        break;
    }
    case Urc::CallEnded:
    case Urc::NoAnswer:
        call_.notify_hangup();
        if (state_ == ModemState::InCall || state_ == ModemState::Ringing) enter(ModemState::Ready);
        break;
    case Urc::CallBusy:
        call_.notify_busy();
        break;
    case Urc::SmsReceived:
        kLog.info("new sms at index %ld", static_cast<long>(ev.arg));
        if (auto_read_ && ev.arg >= 0) read_sms(static_cast<int16_t>(ev.arg));
        break;
    case Urc::SimReady:
        status_.sim_ready = true;
        status_.sim_state = SimState::Ready;
        if (state_ == ModemState::SimMissing)
        {
            kLog.info("SIM detected, resuming to network registration");
            enter(ModemState::WaitingRegistration);
        }
        if (on_sim_) on_sim_(SimState::Ready);
        break;
    case Urc::SimRemoved:
        kLog.error("SIM REMOVED!");
        status_.sim_ready = false;
        status_.sim_state = SimState::Removed;
        enter(ModemState::SimMissing);
        if (on_sim_) on_sim_(SimState::Removed);
        break;
    case Urc::SimInserted:
        kLog.info("SIM INSERTED!");
        status_.sim_state = SimState::Ready;
        enter(ModemState::WaitingRegistration);
        if (on_sim_) on_sim_(SimState::Ready);
        break;
    case Urc::NetworkReg:
        status_.network_ready = (ev.arg == 1 || ev.arg == 5);
        status_.registration  = static_cast<RegState>(ev.arg);
        if (state_ == ModemState::WaitingRegistration && status_.network_ready)
        {
            kLog.info("registered on network (code %ld)", static_cast<long>(ev.arg));
            boot_index_ = 0;
            enter(ModemState::Configuring);
        }
        break;
    case Urc::Ussd:
        ussd_.notify_cusd(ev.line);
        break;
    case Urc::PowerDown:
        kLog.error("module powered down");
        session_.invalidate();
        enter(ModemState::Off);
        break;
    case Urc::ModemReady:
        kLog.debug("module banner");
        break;
    default:
        break;
    }
    user_urc_(ev);
}

void Modem::on_line(const char* line, uint16_t len)
{
    (void)len;
    if (state_ == ModemState::WaitingRegistration)
    {
        if (str_starts_with(line, "+CREG:") || str_starts_with(line, "+CGREG:"))
        {
            int32_t     val   = -1;
            const char* colon = std::strchr(line, ':');
            if (colon)
            {
                char f[8];
                if (csv_field(colon + 1, 1, f, sizeof(f)) && parse_int(f, val)) {}
                else if (csv_field(colon + 1, 0, f, sizeof(f))) { parse_int(f, val); }
            }
            if (val >= 0)
            {
                status_.registration  = static_cast<RegState>(val);
                status_.network_ready = (val == 1 || val == 5);
                if (status_.network_ready)
                {
                    kLog.info("network registered: %s", to_string(status_.registration));
                    boot_index_ = 0;
                    enter(ModemState::Configuring);
                }
            }
        }
    }
    else if (state_ == ModemState::SimMissing)
    {
        if (str_starts_with(line, "+CPIN:"))
        {
            if (std::strstr(line, "READY"))
            {
                kLog.info("SIM recovered: READY detected");
                status_.sim_ready = true;
                status_.sim_state = SimState::Ready;
                if (on_sim_) on_sim_(SimState::Ready);
                enter(ModemState::WaitingRegistration);
            }
        }
    }
}

void Modem::on_final(const AtResult& res)
{
    at_issued_ = false;

    if (res.status == AtStatus::Timeout)
    {
        ++consecutive_timeouts_;
        kLog.warn("Command timed out (consecutive: %u)", consecutive_timeouts_);
    }
    else if (res.ok()) { consecutive_timeouts_ = 0; }

    if (state_ == ModemState::Booting)
    {
        if (res.ok())
        {
            kLog.info("module responsive to AT");
            enter(ModemState::WaitingRegistration);
        }
        else
        {
            if (++at_probe_count_ >= 3)
            {
                if (power_ && pwrkey_pulse_count_ < 2)
                {
                    ++pwrkey_pulse_count_;
                    at_probe_count_ = 0;
                    enter(ModemState::PowerPulsing);
                }
                else
                {
                    kLog.error("no answer from module, gave up");
                    enter(ModemState::Fault);
                }
            }
        }
        return;
    }

    if (state_ == ModemState::Configuring)
    {
        const BootCmd& bc = kBoot[boot_index_];
        if (!res.ok() && !bc.optional)
        {
            kLog.error("bootstrap step %u failed", boot_index_);
            enter(ModemState::Fault);
            return;
        }
        if (boot_index_ == 0) session_.echo_off = res.ok();
        if (boot_index_ == 4) session_.text_mode = res.ok();
        if (boot_index_ == 5) session_.charset = res.ok() ? Charset::Ucs2 : Charset::Unknown;
        if (boot_index_ == 6) session_.csmp_ucs2 = res.ok();
        if (boot_index_ == 7) session_.storage_ready = res.ok();
        if (++boot_index_ >= kBootCount)
        {
            kLog.info("ready");
            enter(ModemState::Ready);
            last_signal_ = last_sweep_ = last_scan_ = last_cleanup_ = clock_.millis();
            refresh_status();
        }
        return;
    }

    if (state_ == ModemState::SimMissing)
    {
        if (sim_recovery_phase_ == 1) { sim_recovery_phase_ = 2; }
        else if (sim_recovery_phase_ == 2) { sim_recovery_phase_ = 0; }
        return;
    }
}

void Modem::pump_services()
{
    if (current_.state == JobState::Running)
    {
        IService* svc = service_for(current_.kind);
        if (svc)
        {
            if (svc->active())
            {
                svc->pump();
                return;
            }
            finish_job(svc->result());
            return;
        }
    }
    if (current_.kind != JobKind::None && current_.state == JobState::Queued)
    {
        // Call gate: hold non-call jobs while a call is engaged so a call that
        // lands between SMS jobs can never corrupt or stall the queue.
        if (call_gate() && !is_call_job(current_.kind)) return;
        if (start_job(current_))
        {
            start_failures_ = 0;
            return;
        }
        // Service refused to start: retry briefly, then fail the job so the
        // queue can never wedge forever on a stuck service state.
        if (++start_failures_ >= GSM_JOB_START_RETRIES)
        {
            start_failures_ = 0;
            IService* svc   = service_for(current_.kind);
            AtResult  res   = svc ? svc->result() : AtResult{AtStatus::Error, kErrJobStalled};
            if (res.status == AtStatus::Pending) res = AtResult{AtStatus::Error, kErrJobStalled};
            kLog.error("#%u %s cannot start, dropping it", current_.id, to_string(current_.kind));
            finish_job(res);
        }
        return;
    }

    if (current_.kind != JobKind::None) return;
    Job         job;
    JobCallback cb;
    if (!queue_.pop(job, cb)) return;
    current_    = job;
    current_cb_ = cb;
    if (call_gate() && !is_call_job(current_.kind))
    {
        current_.state = JobState::Queued; // held until the call ends
        return;
    }
    if (!start_job(current_))
    {
        IService* svc = service_for(current_.kind);
        AtResult  res = svc ? svc->result() : AtResult{AtStatus::Error, kErrJobStalled};
        if (res.status == AtStatus::Pending) res = AtResult{AtStatus::Error, kErrJobStalled};
        finish_job(res);
    }
}

void Modem::pump_housekeeping()
{
    if (!hk_.enabled || call_gate()) return;
    const uint32_t now = clock_.millis();
    if (hk_.full_sweep_ms && hal::elapsed(now, last_sweep_, hk_.full_sweep_ms))
    {
        last_sweep_ = last_signal_ = now;
        stat_.start_full_sweep();
        return;
    }
    if (hk_.signal_ms && hal::elapsed(now, last_signal_, hk_.signal_ms))
    {
        last_signal_ = now;
        stat_.start(StatusStep::Signal);
        return;
    }
    if (hk_.sms_scan_ms && hal::elapsed(now, last_scan_, hk_.sms_scan_ms))
    {
        last_scan_ = now;
        scan_sms();
        return;
    }
    if (hk_.cleanup_ms && hal::elapsed(now, last_cleanup_, hk_.cleanup_ms))
    {
        last_cleanup_ = now;
        if (status_.sms_used) delete_read_sms();
        return;
    }
}

void Modem::check_watchdog()
{
    const uint32_t now = clock_.millis();

    // 1. Purge stale queued jobs (prevents queue buildup when modem is offline/busy)
    uint8_t purged = queue_.purge_stale(now, GSM_JOB_MAX_AGE_MS);
    if (purged > 0)
    {
        kLog.warn("Purged %u stale jobs from queue (exceeded %lu ms age)", purged,
                  static_cast<unsigned long>(GSM_JOB_MAX_AGE_MS));
        flush_cancelled();
    }

    // 2. Consecutive timeouts recovery
    if (consecutive_timeouts_ >= GSM_CONSECUTIVE_TIMEOUTS_RESET)
    {
        kLog.error("Watchdog: %u consecutive AT timeouts! Initiating hardware recovery...",
                   consecutive_timeouts_);
        consecutive_timeouts_ = 0;
        stop_all(false);
        if (power_) { power_cycle(); }
        else
        {
            reset();
        }
        return;
    }

    // 3. Overall hardware reboot timeout if stuck in non-operational state
    if (power_ && (state_ == ModemState::Fault || state_ == ModemState::SimMissing))
    {
        if (hal::elapsed(now, state_since_, GSM_HARDWARE_REBOOT_TIMEOUT_MS))
        {
            kLog.error(
                "Watchdog: Modem stuck in %s for %lu ms, triggering full hardware power cycle!",
                to_string(state_), static_cast<unsigned long>(GSM_HARDWARE_REBOOT_TIMEOUT_MS));
            state_since_ = now;
            power_cycle();
            return;
        }
    }
}

void Modem::handle_sim_recovery()
{
    if (state_ != ModemState::SimMissing || at_issued_ || !at_.idle()) return;

    const uint32_t now = clock_.millis();

    // Phase 2: send AT+CFUN=1 after CFUN=0 completed
    if (sim_recovery_phase_ == 2)
    {
        kLog.info("SIM recovery: sending AT+CFUN=1 (re-enable RF/SIM layer)");
        at_issued_ = at_.execute("AT+CFUN=1", 10000, this);
        return;
    }

    // Regular interval: Check if it's time for CFUN cycle (AT+CFUN=0 then 1)
    if (hal::elapsed(now, last_cfun_cycle_, GSM_CFUN_CYCLE_INTERVAL_MS))
    {
        last_cfun_cycle_ = last_sim_probe_ = now;
        sim_recovery_phase_                = 1;
        kLog.info("SIM recovery: initiating periodic CFUN cycle (AT+CFUN=0)...");
        at_issued_ = at_.execute("AT+CFUN=0", 5000, this);
        return;
    }

    // Periodic probe: Check AT+CPIN?
    if (hal::elapsed(now, last_sim_probe_, GSM_SIM_RETRY_INTERVAL_MS))
    {
        last_sim_probe_ = now;
        kLog.info("SIM missing: probing AT+CPIN? to check if SIM was re-inserted");
        at_issued_ = at_.execute("AT+CPIN?", 3000, this);
        return;
    }
}

void Modem::tick()
{
    at_.tick();
    flush_cancelled();

    // Check industrial watchdog & recover stuck conditions
    check_watchdog();

    // Call supervision: hangup/reject timers and +CLCC safety net (self driven,
    // so call tracking never depends on being the current queue job)
    call_.monitor(clock_.millis(), at_.idle());
    if (call_.active() && at_.idle()) call_.pump();

    // Dual maintenance resetters (time budget + ops budget)
    maintenance_service();

    switch (state_)
    {
    case ModemState::Off:
    case ModemState::Fault:
        return;

    case ModemState::SimMissing:
        handle_sim_recovery();
        return;

    case ModemState::Booting:
        // Wait out the boot guard before probing: right after a PWRKEY release
        // the module is still booting (RDY / +CFUN: 1 / SIM Ready storm).
        if (!at_issued_ && at_.idle() && boot_guard_elapsed())
            at_issued_ = at_.execute("AT", 1000, this);
        return;

    case ModemState::PowerPulsing:
        if (!power_)
        {
            enter(ModemState::Booting);
            return;
        }
        if (!pwrkey_asserted_)
        {
            start_pwrkey_pulse();
            return;
        }
        if (hal::elapsed(clock_.millis(), pwrkey_since_, pwrkey_pulse_ms_))
        {
            release_pwrkey();
            enter(ModemState::Booting);
        }
        return;

    case ModemState::WaitingRegistration:
        if (hal::elapsed(clock_.millis(), state_since_, GSM_REGISTRATION_TIMEOUT))
        {
            kLog.error("network registration timeout after %lums",
                       static_cast<unsigned long>(GSM_REGISTRATION_TIMEOUT));
            enter(ModemState::Fault);
            return;
        }
        if (!at_issued_ && at_.idle())
        {
            if (hal::elapsed(clock_.millis(), last_reg_check_, 2000))
            {
                last_reg_check_ = clock_.millis();
                at_issued_      = at_.execute("AT+CREG?", 1000, this);
            }
        }
        return;

    case ModemState::Configuring: {
        if (at_issued_ || !at_.idle()) return;
        const BootCmd& bc      = kBoot[boot_index_];
        const char*    cmd     = bc.cmd;
        uint32_t       timeout = bc.timeout_ms ? bc.timeout_ms : 1000;
        if (boot_index_ == 6) cmd = traits_.csmp_ucs2;
        if (boot_index_ == 7) cmd = traits_.storage_cmd;
        if (!cmd)
        {
            AtResult res{AtStatus::Ok, -1};
            on_final(res);
            return;
        }
        at_issued_ = at_.execute(cmd, timeout, this);
        return;
    }

    default:
        break;
    }

    if (!at_.idle()) return;

    if (stat_.active() && current_.kind != JobKind::StatusSweep)
    {
        stat_.pump();
        return;
    }
    pump_services();

    if (current_.kind == JobKind::None && at_.idle() && !stat_.active())
    {
        if (call_.in_call())
            enter(ModemState::InCall);
        else if (call_.ringing())
            enter(ModemState::Ringing);
        else
        {
            enter(ModemState::Ready);
            pump_housekeeping();
        }
    }

    // Health verification probes run only when everything else is idle
    watchdog_.pump(at_, at_.idle() && current_.kind == JobKind::None && !stat_.active(),
                   state_ == ModemState::Ready);
}

} // namespace gsm
