#include "gsm/maintenance/watchdog.hpp"
#include "gsm/log/log.hpp"
#include "gsm/util/static_string.hpp"
#include <cstdio>
#include <cstring>

namespace gsm
{

static const log::Channel kLog{"watchdog", log::Level::Info};

const char* to_string(ResetReason r)
{
    switch (r)
    {
    case ResetReason::None:
        return "none";
    case ResetReason::Periodic:
        return "periodic";
    case ResetReason::OpsBudget:
        return "ops-budget";
    case ResetReason::Manual:
        return "manual";
    case ResetReason::FaultRecovery:
        return "fault-recovery";
    }
    return "?";
}

const char* to_string(HealthState s)
{
    switch (s)
    {
    case HealthState::Unknown:
        return "unknown";
    case HealthState::Checking:
        return "checking";
    case HealthState::Healthy:
        return "healthy";
    case HealthState::Unhealthy:
        return "unhealthy";
    }
    return "?";
}

void MaintenanceWatchdog::note_operation()
{ ++ops_; }

void MaintenanceWatchdog::tick(uint32_t now_ms)
{
    if (!policy_.enabled || pending_ != ResetReason::None) return;

    if (policy_.periodic_ms && started_ &&
        hal::elapsed(now_ms, last_reset_ms_, policy_.periodic_ms))
    {
        kLog.warn("time budget expired (%lu ms) -> scheduling full reset",
                  static_cast<unsigned long>(policy_.periodic_ms));
        request_reset(ResetReason::Periodic);
        return;
    }

    if (policy_.ops_period && ops_ >= policy_.ops_period)
    {
        kLog.warn("ops budget expired (%u ops) -> scheduling full reset", ops_);
        request_reset(ResetReason::OpsBudget);
    }
}

void MaintenanceWatchdog::request_reset(ResetReason r)
{
    if (pending_ != ResetReason::None) return;
    pending_ = r;
}

ResetReason MaintenanceWatchdog::take_pending()
{
    const ResetReason r = pending_;
    pending_            = ResetReason::None;
    return r;
}

void MaintenanceWatchdog::on_reset_started(uint32_t now_ms)
{
    last_reset_ms_ = now_ms;
    ops_           = 0;
    started_       = true;
    ++resets_;
    health_        = HealthReport{};
    health_.resets = resets_;
    check_pending_ = policy_.health_check;
    kLog.info("full module reset #%lu done budget clocks cleared",
              static_cast<unsigned long>(resets_));
}

void MaintenanceWatchdog::start_check()
{
    check_pending_ = false;
    probe_index_   = 0;
    issued_        = false;
    health_        = HealthReport{};
    health_.resets = resets_;
    health_.state  = HealthState::Checking;
    kLog.info("health check started");
}

void MaintenanceWatchdog::cancel_check()
{
    check_pending_ = false;
    probe_index_   = 0;
    issued_        = false;
    health_        = HealthReport{};
    health_.resets = resets_;
}

void MaintenanceWatchdog::pump(AtEngine& at, bool engine_idle, bool modem_ready)
{
    if (check_pending_)
    {
        if (modem_ready && engine_idle) start_check();
        return;
    }
    if (health_.state != HealthState::Checking) return;
    if (!engine_idle || issued_) return;

    switch (probe_index_)
    {
    case 0:
        issued_ = at.execute("AT", 1000, this);
        return;
    case 1:
        issued_ = at.execute("AT+CPIN?", 3000, this);
        return;
    case 2:
        issued_ = at.execute("AT+CREG?", 2000, this);
        return;
    case 3:
        issued_ = at.execute("AT+CSQ", 2000, this);
        return;
    default:
        finish_check();
        return;
    }
}

void MaintenanceWatchdog::on_line(const char* line, uint16_t len)
{
    (void)len;
    if (health_.state != HealthState::Checking) return;

    // +CPIN / +CREG lines are swallowed by the UrcRouter and land in the live
    // ModemStatus instead, so SIM / network state is read from there.
    if (probe_index_ == 3 && str_starts_with(line, "+CSQ:"))
    {
        int32_t rssi = -1;
        parse_int(line + 5, rssi);
        if (rssi >= 0 && rssi != 99) health_.rssi_dbm = static_cast<int16_t>(-113 + 2 * rssi);
    }
}

void MaintenanceWatchdog::on_final(const AtResult& res)
{
    issued_ = false;
    if (health_.state != HealthState::Checking) return;

    if (probe_index_ == 0 && res.ok()) health_.at_ok = true;
    ++probe_index_;
}

void MaintenanceWatchdog::finish_check()
{
    if (status_)
    {
        health_.sim_ok = status_->sim_ready;
        health_.net_ok = status_->network_ready;
    }
    const bool ok = health_.at_ok && health_.sim_ok && health_.net_ok;
    health_.state = ok ? HealthState::Healthy : HealthState::Unhealthy;
    if (ok)
        kLog.info("health: OK (rssi=%ddBm, resets=%lu)", health_.rssi_dbm,
                  static_cast<unsigned long>(health_.resets));
    else
        kLog.error("health: FAILED (at=%d sim=%d net=%d)", (int)health_.at_ok, (int)health_.sim_ok,
                   (int)health_.net_ok);
    if (on_health_) on_health_(health_);
}

} // namespace gsm
