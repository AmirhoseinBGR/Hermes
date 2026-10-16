#include "gsm/status/status_service.hpp"
#include "gsm/log/log.hpp"
#include <cstring>
#include <stdio.h>

namespace gsm
{

static const log::Channel kLog{"stat", log::Level::Debug};

const char* to_string(RegState s)
{
    switch (s)
    {
    case RegState::NotRegistered:
        return "NotRegistered";
    case RegState::Home:
        return "Home";
    case RegState::Searching:
        return "Searching";
    case RegState::Denied:
        return "Denied";
    case RegState::Unknown:
        return "Unknown";
    case RegState::Roaming:
        return "Roaming";
    }
    return "?";
}

const char* to_string(SimState s)
{
    switch (s)
    {
    case SimState::Unknown:
        return "Unknown";
    case SimState::Ready:
        return "Ready";
    case SimState::Removed:
        return "Removed";
    case SimState::LockedPin:
        return "LockedPin";
    case SimState::LockedPuk:
        return "LockedPuk";
    case SimState::Error:
        return "Error";
    }
    return "?";
}

const char* to_string(Operator op)
{
    switch (op)
    {
    case Operator::Irancell:
        return "Irancell";
    case Operator::Mci:
        return "MCI (Hamrahe Aval)";
    case Operator::Rightel:
        return "RighTel";
    case Operator::Unknown:
        return "Unknown";
    }
    return "?";
}

Operator detect_operator(const char* name)
{
    if (!name || !*name) return Operator::Unknown;
    // Irancell identifiers
    if (std::strstr(name, "Irancell") || std::strstr(name, "MTN") ||
        std::strstr(name, "IRANCELL") || std::strstr(name, "43235") ||
        std::strstr(name, "irancell"))
        return Operator::Irancell;
    // MCI / Hamrah-e Aval identifiers
    if (std::strstr(name, "MCI") || std::strstr(name, "TCI") || std::strstr(name, "IR-MCI") ||
        std::strstr(name, "IR-TCI") || std::strstr(name, "Hamrah") || std::strstr(name, "43211"))
        return Operator::Mci;
    // Rightel identifiers
    if (std::strstr(name, "Rightel") || std::strstr(name, "RighTel") ||
        std::strstr(name, "RIGHTEL") || std::strstr(name, "43220"))
        return Operator::Rightel;

    return Operator::Unknown;
}

const char* to_string(StatusStep s)
{
    switch (s)
    {
    case StatusStep::Idle:
        return "idle";
    case StatusStep::Signal:
        return "csq";
    case StatusStep::Registration:
        return "creg";
    case StatusStep::SimPin:
        return "cpin";
    case StatusStep::Storage:
        return "cpms";
    case StatusStep::Operator:
        return "cops";
    case StatusStep::Done:
        return "done";
    }
    return "?";
}

bool StatusService::start(StatusStep first)
{
    if (active()) return false;
    step_   = first;
    issued_ = false;
    sweep_  = false;
    result_ = AtResult{};
    return true;
}

bool StatusService::start_full_sweep()
{
    if (!start(StatusStep::Signal)) return false;
    sweep_ = true;
    return true;
}

void StatusService::cancel()
{
    if (active()) at_.abort();
    step_   = StatusStep::Idle;
    issued_ = false;
}

void StatusService::pump()
{
    if (!active() || issued_) return;
    switch (step_)
    {
    case StatusStep::Signal:
        issued_ = at_.execute("AT+CSQ", 1000, this);
        return;
    case StatusStep::Registration:
        issued_ = at_.execute("AT+CREG?", 1000, this);
        return;
    case StatusStep::SimPin:
        issued_ = at_.execute("AT+CPIN?", 5000, this);
        return;
    case StatusStep::Storage:
        if (!traits_.storage_cmd)
        {
            step_ = StatusStep::Operator;
            return;
        }
        issued_ = at_.execute("AT+CPMS?", 2000, this);
        return;
    case StatusStep::Operator:
        issued_ = at_.execute("AT+COPS?", 5000, this);
        return;
    default:
        return;
    }
}

void StatusService::on_line(const char* line, uint16_t len)
{
    (void)len;
    if (str_starts_with(line, "+CSQ:"))
    {
        char f[8];
        if (csv_field(line + 5, 0, f, sizeof(f)))
        {
            int32_t v = 0;
            if (parse_int(f, v))
            {
                status_.csq_raw  = static_cast<uint8_t>(v);
                status_.rssi_dbm = (v == 99) ? 0 : static_cast<int8_t>(-113 + 2 * v);
            }
        }
        if (csv_field(line + 5, 1, f, sizeof(f)))
        {
            int32_t v = 0;
            if (parse_int(f, v)) status_.ber = static_cast<uint8_t>(v);
        }
        return;
    }
    if (str_starts_with(line, "+CREG:"))
    {
        char f[8];
        if (csv_field(line + 6, 1, f, sizeof(f)))
        {
            int32_t v = 0;
            if (parse_int(f, v))
            {
                status_.registration  = static_cast<RegState>(v);
                status_.network_ready = (v == 1 || v == 5);
            }
        }
        return;
    }
    if (str_starts_with(line, "+CPIN:"))
    {
        printf("===> line: %s \n", line);
        if (std::strstr(line, "NOT READY"))
        {
            status_.sim_state = SimState::Removed;
            status_.sim_ready = false;
            printf("noooot ready\n");
        }
        else if (std::strstr(line, "READY"))
        {
            status_.sim_state = SimState::Ready;
            status_.sim_ready = true;
            printf("====> status: %s \n", to_string(status_.sim_state));
        }
        else if (std::strstr(line, "NOT INSERTED"))
        {
            status_.sim_state = SimState::Removed;
            status_.sim_ready = false;
        }
        else if (std::strstr(line, "SIM PIN"))
        {
            status_.sim_state = SimState::LockedPin;
            status_.sim_ready = false;
        }
        else if (std::strstr(line, "SIM PUK"))
        {
            status_.sim_state = SimState::LockedPuk;
            status_.sim_ready = false;
        }
        else
        {
            status_.sim_state = SimState::Error;
            status_.sim_ready = false;
        }
        return;
    }
    if (str_starts_with(line, "+CPMS:"))
    {
        char f[12];
        if (csv_field(line + 6, 1, f, sizeof(f)))
        {
            int32_t v = 0;
            if (parse_int(f, v)) status_.sms_used = static_cast<uint8_t>(v);
        }
        if (csv_field(line + 6, 2, f, sizeof(f)))
        {
            int32_t v = 0;
            if (parse_int(f, v)) status_.sms_total = static_cast<uint8_t>(v);
        }
        return;
    }
    if (str_starts_with(line, "+COPS:"))
    {
        char f[32];
        if (csv_field(line + 6, 2, f, sizeof(f)))
        {
            status_.operator_name.assign(f);
            status_.op = detect_operator(f);
        }
        return;
    }
}

void StatusService::on_final(const AtResult& res)
{
    issued_               = false;
    result_               = res;
    status_.updated_at_ms = at_.now();
    if (!sweep_)
    {
        step_ = StatusStep::Idle;
        return;
    }
    switch (step_)
    {
    case StatusStep::Signal:
        step_ = StatusStep::Registration;
        break;
    case StatusStep::Registration:
        step_ = StatusStep::SimPin;
        break;
    case StatusStep::SimPin:
        step_ = StatusStep::Storage;
        break;
    case StatusStep::Storage:
        step_ = StatusStep::Operator;
        break;
    default:
        kLog.debug("csq=%u(%ddBm) reg=%u sim=%s sms=%u/%u op=%s", status_.csq_raw, status_.rssi_dbm,
                   static_cast<unsigned>(status_.registration), to_string(status_.sim_state),
                   status_.sms_used, status_.sms_total, to_string(status_.op));
        step_ = StatusStep::Idle;
        break;
    }
}

} // namespace gsm
