#pragma once
#include "gsm/core/service.hpp"
#include "gsm/device/device_traits.hpp"
#include "gsm/util/static_string.hpp"

namespace gsm
{

enum class RegState : uint8_t
{
    NotRegistered = 0,
    Home          = 1,
    Searching     = 2,
    Denied        = 3,
    Unknown       = 4,
    Roaming       = 5
};

enum class SimState : uint8_t
{
    Unknown = 0,
    Ready,
    Removed,
    LockedPin,
    LockedPuk,
    Error
};

enum class Operator : uint8_t
{
    Unknown = 0,
    Irancell, // MTN Irancell
    Mci,      // Hamrah-e Aval / MCI / IR-TCI
    Rightel   // RighTel
};

const char* to_string(RegState s);
const char* to_string(SimState s);
const char* to_string(Operator op);
Operator    detect_operator(const char* name);

struct ModemStatus
{
    uint8_t          csq_raw       = 99;
    int8_t           rssi_dbm      = 0;
    uint8_t          ber           = 99;
    RegState         registration  = RegState::Unknown;
    SimState         sim_state     = SimState::Unknown;
    bool             sim_ready     = false;
    bool             network_ready = false;
    uint8_t          sms_used      = 0;
    uint8_t          sms_total     = 0;
    Operator         op            = Operator::Unknown;
    StaticString<32> operator_name;
    uint32_t         updated_at_ms = 0;

    uint8_t signal_bars() const
    {
        if (csq_raw == 99) return 0;
        if (csq_raw >= 20) return 5;
        if (csq_raw >= 15) return 4;
        if (csq_raw >= 11) return 3;
        if (csq_raw >= 7) return 2;
        return 1;
    }
    bool storage_full() const { return sms_total && sms_used >= sms_total; }
};

enum class StatusStep : uint8_t
{
    Idle = 0,
    Signal,
    Registration,
    SimPin,
    Storage,
    Operator,
    Done
};
const char* to_string(StatusStep s);

class StatusService final : public IService
{
  public:
    StatusService(AtEngine& engine, ModemStatus& status, const DeviceTraits& traits)
        : at_(engine), status_(status), traits_(traits)
    {
    }

    bool start(StatusStep first = StatusStep::Signal);
    bool start_full_sweep();

    bool            active() const override { return step_ != StatusStep::Idle; }
    void            pump() override;
    void            cancel() override;
    const char*     step_name() const override { return to_string(step_); }
    const AtResult& result() const override { return result_; }
    void            on_line(const char* line, uint16_t len) override;
    void            on_final(const AtResult& res) override;

  private:
    AtEngine&           at_;
    ModemStatus&        status_;
    const DeviceTraits& traits_;
    StatusStep          step_   = StatusStep::Idle;
    bool                issued_ = false;
    bool                sweep_  = false;
    AtResult            result_;
};

} // namespace gsm
