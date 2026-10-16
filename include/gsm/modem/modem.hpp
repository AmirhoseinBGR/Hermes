#pragma once
#include "gsm/core/at_engine.hpp"
#include "gsm/core/service.hpp"
#include "gsm/device/device_traits.hpp"
#include "gsm/maintenance/watchdog.hpp"
#include "gsm/queue/job_queue.hpp"
#include "gsm/sms/sms_service.hpp"
#include "gsm/status/status_service.hpp"
#include "gsm/ussd/ussd_service.hpp"
#include "gsm/voice/call_service.hpp"

namespace gsm
{

enum class ModemState : uint8_t
{
    Off = 0,
    Booting,             // Sending AT probes
    PowerPulsing,        // Pulsing PWRKEY pin if no AT response
    WaitingRegistration, // Waiting for network registration (+CREG=1 or 5)
    Configuring,         // Applying bootstrap commands
    Ready,               // Idle, housekeeping active
    Working,             // Executing a job from queue
    Ringing,             // Incoming call
    InCall,              // Voice call active
    SimMissing,          // SIM card not inserted / removed
    Fault,               // Gave up (AT unresponsiveness or timeout)
};

const char* to_string(ModemState s);

struct HousekeepingConfig
{
    bool     enabled       = true;
    uint32_t signal_ms     = 15000;
    uint32_t full_sweep_ms = 60000;
    uint32_t sms_scan_ms   = 25000;
    uint32_t cleanup_ms    = 300000;
};

class Modem final : public IAtObserver
{
  public:
    using SimEventCb = Delegate<SimState>;

    Modem(hal::IUart& uart, hal::IClock& clock, const DeviceTraits& traits = kSim800,
          hal::IPowerControl* power = nullptr);

    void begin();
    void tick();
    void reset();
    void power_cycle();

    // Hardware power control helpers
    bool has_power_control() const { return power_ != nullptr; }
    void pulse_pwrkey(uint32_t pulse_ms = GSM_PWRKEY_PULSE_MS);

    // --- jobs ---------------------------------------------------------------
    JobId send_sms(const char* number, const char* utf8_text, JobCallback cb = {},
                   SmsEncoding enc = SmsEncoding::Auto);
    JobId read_sms(int16_t index, JobCallback cb = {});
    JobId scan_sms(JobCallback cb = {});
    JobId delete_read_sms(JobCallback cb = {});
    JobId dial(const char* number, JobCallback cb = {});
    JobId answer(JobCallback cb = {});
    JobId hang_up(JobCallback cb = {});
    JobId refresh_status(JobCallback cb = {});
    JobId ussd(const char* code, JobCallback cb = {});
    JobId check_balance(JobCallback cb = {});

    // --- queue control ------------------------------------------------------
    void       pause_queue() { queue_.pause(); }
    void       resume_queue() { queue_.resume(); }
    bool       queue_paused() const { return queue_.paused(); }
    void       stop_all(bool also_pause = true);
    bool       cancel(JobId id);
    uint8_t    queue_size() const { return queue_.size(); }
    const Job* queued(uint8_t i) const { return queue_.at(i); }
    const Job& current_job() const { return current_; }

    // --- observation --------------------------------------------------------
    ModemState           state() const { return state_; }
    const char*          state_name() const { return to_string(state_); }
    const char*          activity() const { return to_string(current_.kind); }
    const char*          step() const;
    bool                 ready() const { return state_ == ModemState::Ready; }
    const ModemStatus&   status() const { return status_; }
    SmsService&          sms() { return sms_; }
    CallService&         call() { return call_; }
    StatusService&       stat() { return stat_; }
    UssdService&         ussd_service() { return ussd_; }
    AtEngine&            at() { return at_; }
    HousekeepingConfig&  housekeeping() { return hk_; }
    MaintenanceWatchdog& maintenance() { return watchdog_; }
    CallPolicy&          call_policy() { return call_.policy(); }
    // True while a call is engaged: non-call jobs are held so a call landing
    // between SMS jobs can never corrupt or stall the queue.
    bool call_gate() const;

    void set_on_sms(SmsService::Received cb) { sms_.set_on_received(cb); }
    void set_on_call(CallService::StateChanged cb) { call_.set_on_state(cb); }
    void set_on_call_ended(CallService::Ended cb) { call_.set_on_ended(cb); }
    void set_on_health(MaintenanceWatchdog::ReportCb cb) { watchdog_.set_on_health(cb); }
    void set_on_job(JobCallback cb) { on_job_ = cb; }
    void set_on_urc(UrcRouter::Handler cb) { user_urc_ = cb; }
    void set_on_sim(SimEventCb cb) { on_sim_ = cb; }
    void set_auto_read_sms(bool on) { auto_read_ = on; }

    // IAtObserver: used for bootstrap and probes
    void on_line(const char* line, uint16_t len) override;
    void on_final(const AtResult& res) override;

  private:
    void        handle_urc(const UrcEvent& ev);
    static void urc_trampoline(void* ctx, const UrcEvent& ev);
    void        enter(ModemState s);
    JobId       enqueue(Job& job, JobCallback cb);
    bool        start_job(Job& job);
    void        finish_job(const AtResult& res);
    void        pump_services();
    void        pump_housekeeping();
    void        flush_cancelled();
    IService*   service_for(JobKind kind);

    // Watchdog and SIM recovery helpers
    void check_watchdog();
    void handle_sim_recovery();

    // Call / queue interplay and maintenance helpers
    static bool is_call_job(JobKind kind);
    void        preempt_for_call();
    void        maintenance_service();
    void        full_restart(bool hardware, bool keep_jobs);

    hal::IUart&         uart_;
    hal::IClock&        clock_;
    const DeviceTraits& traits_;
    hal::IPowerControl* power_;

    UrcRouter     urc_;
    AtEngine      at_;
    SessionState  session_;
    ModemStatus   status_;
    SmsService    sms_;
    CallService   call_;
    StatusService stat_;
    UssdService   ussd_;

    JobQueue           queue_;
    Job                current_;
    JobCallback        current_cb_;
    JobCallback        on_job_;
    UrcRouter::Handler user_urc_;
    SimEventCb         on_sim_;

    ModemState         state_              = ModemState::Off;
    uint8_t            at_probe_count_     = 0;
    uint8_t            pwrkey_pulse_count_ = 0;
    uint8_t            boot_index_         = 0;
    bool               at_issued_          = false;
    uint32_t           state_since_        = 0;
    uint32_t           last_reg_check_     = 0;
    uint32_t           last_signal_        = 0;
    uint32_t           last_sweep_         = 0;
    uint32_t           last_scan_          = 0;
    uint32_t           last_cleanup_       = 0;
    JobId              next_id_            = 1;
    bool               auto_read_          = true;
    HousekeepingConfig hk_;

    // Industrial reliability & recovery state
    uint8_t  consecutive_timeouts_ = 0;
    uint32_t last_sim_probe_       = 0;
    uint32_t last_cfun_cycle_      = 0;
    uint8_t  sim_recovery_phase_   = 0; // 0=probe AT+CPIN?, 1=CFUN=0, 2=CFUN=1

    // Call gate + maintenance resetters
    uint8_t             start_failures_    = 0;
    uint32_t            maintenance_since_ = 0;
    MaintenanceWatchdog watchdog_;
};

} // namespace gsm
