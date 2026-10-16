#pragma once
#include "gsm/core/service.hpp"
#include "gsm/device/device_traits.hpp"
#include "gsm/gsm_config.hpp"
#include "gsm/status/status_service.hpp"
#include "gsm/util/delegate.hpp"
#include "gsm/util/static_string.hpp"

namespace gsm
{

struct UssdResult
{
    StaticString<GSM_USSD_RESPONSE_BYTES> response;
    AtResult                              result;
    int16_t status_code = -1; // 0=no further action, 1=further action needed
};

enum class UssdStep : uint8_t
{
    Idle = 0,
    SetUssdMode,
    SendQuery,
    SendReply,   // sending a reply inside an already-open interactive session
    SessionOpen, // network asked for more input (CUSD m==1), waiting for send_reply()/end_session()
    EndSession,  // sending AT+CUSD=2 to close the session
    WaitUrc,
    Done
};

const char* to_string(UssdStep s);

class UssdService final : public IService
{
  public:
    using UssdDone = Delegate<const UssdResult&>;

    UssdService(AtEngine& engine, SessionState& session, const DeviceTraits& traits)
        : at_(engine), session_(session), traits_(traits)
    {
    }

    bool start_query(const char* ussd_code);
    // Auto query balance using detected operator
    bool start_balance_query(Operator op);

    bool            active() const override { return step_ != UssdStep::Idle; }
    void            pump() override;
    void            cancel() override;
    const char*     step_name() const override { return to_string(step_); }
    const AtResult& result() const override { return report_.result; }

    void on_line(const char* line, uint16_t len) override;
    void on_final(const AtResult& res) override;

    // Handles +CUSD: <m>,"<str>",<dcs>
    void notify_cusd(const char* line);

    const UssdResult& last_result() const { return report_; }
    void              set_on_complete(UssdDone cb) { on_done_ = cb; }

    static const char* default_balance_code(Operator op);

    bool send_reply(const char* text);
    bool end_session();
    bool session_open() const;

  private:
    void goto_step(UssdStep s)
    {
        step_   = s;
        issued_ = false;
    }
    void complete(AtStatus st = AtStatus::Ok, int16_t code = -1);

    AtEngine&           at_;
    SessionState&       session_;
    const DeviceTraits& traits_;
    UssdStep            step_   = UssdStep::Idle;
    bool                issued_ = false;
    StaticString<32>    code_;
    UssdResult          report_;
    UssdDone            on_done_;
    uint32_t            wait_start_ = 0;

    bool session_open_ = false;
};

} // namespace gsm
