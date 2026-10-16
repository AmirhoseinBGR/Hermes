#pragma once
#include "gsm/core/at_engine.hpp"
#include "gsm/gsm_config.hpp"
#include "gsm/status/status_service.hpp"
#include "gsm/util/delegate.hpp"

namespace gsm
{

enum class ResetReason : uint8_t
{
    None = 0,
    Periodic,  // resetter #1: time budget expired
    OpsBudget, // resetter #2: operation budget expired
    Manual,
    FaultRecovery
};

enum class HealthState : uint8_t
{
    Unknown = 0,
    Checking,
    Healthy,
    Unhealthy
};

const char* to_string(ResetReason r);
const char* to_string(HealthState s);

struct HealthReport
{
    HealthState state          = HealthState::Unknown;
    bool        at_ok          = false;
    bool        sim_ok         = false;
    bool        net_ok         = false;
    int16_t     rssi_dbm       = 0;
    uint32_t    resets         = 0;
    uint32_t    since_reset_ms = 0;
    bool        ok() const { return state == HealthState::Healthy; }
};

// Two independent reset budgets (both configurable at compile and run time):
//   #1 periodic_ms : full module reset every N ms (default 1 hour)
//   #2 ops_period  : full module reset after N completed operations
// After every reset a health check (AT / CPIN / CREG / CSQ) verifies the module.
struct ResetPolicy
{
    bool     enabled         = true;
    uint32_t periodic_ms     = GSM_PERIODIC_RESET_MS;
    uint16_t ops_period      = GSM_OPS_RESET_COUNT;
    uint32_t grace_ms        = GSM_WATCHDOG_GRACE_MS;
    bool     use_power_cycle = false; // false: AT+CFUN=1,1; true: PWRKEY if available
    bool     health_check    = true;
};

class MaintenanceWatchdog final : public IAtObserver
{
  public:
    using ReportCb = Delegate<const HealthReport&>;

    // --- budgets ---------------------------------------------------------
    void        note_operation();      // one finished operation (job)
    void        tick(uint32_t now_ms); // evaluate both budgets
    bool        reset_pending() const { return pending_ != ResetReason::None; }
    ResetReason take_pending();
    void        request_reset(ResetReason r = ResetReason::Manual);
    void        on_reset_started(uint32_t now_ms);

    // --- health check ----------------------------------------------------
    void                bind_status(const ModemStatus* st) { status_ = st; }
    void                start_check();
    void                cancel_check(); // abandon an in-flight health check
    bool                check_busy() const { return health_.state == HealthState::Checking; }
    void                pump(AtEngine& at, bool engine_idle, bool modem_ready);
    const HealthReport& health() const { return health_; }

    void set_on_health(ReportCb cb) { on_health_ = cb; }

    // IAtObserver
    void on_line(const char* line, uint16_t len) override;
    void on_final(const AtResult& res) override;

    ResetPolicy&       policy() { return policy_; }
    const ResetPolicy& policy() const { return policy_; }
    uint32_t           ops_count() const { return ops_; }
    uint32_t           resets_done() const { return resets_; }
    uint32_t           since_reset_ms(uint32_t now) const { return now - last_reset_ms_; }

  private:
    void finish_check();

    ResetPolicy policy_;
    uint32_t    last_reset_ms_ = 0;
    uint32_t    ops_           = 0;
    uint32_t    resets_        = 0;
    bool        started_       = false; // a reset/boot has been timestamped
    ResetReason pending_       = ResetReason::None;

    uint8_t            probe_index_   = 0;
    bool               issued_        = false;
    bool               check_pending_ = false;
    const ModemStatus* status_        = nullptr; // live SIM / network state (URC maintained)
    HealthReport       health_;
    ReportCb           on_health_;
};

} // namespace gsm
