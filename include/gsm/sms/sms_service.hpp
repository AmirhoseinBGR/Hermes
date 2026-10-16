#pragma once
#include "gsm/core/service.hpp"
#include "gsm/device/device_traits.hpp"
#include "gsm/sms/sms_types.hpp"
#include "gsm/util/delegate.hpp"

namespace gsm
{

enum class SmsStep : uint8_t
{
    Idle = 0,
    SetTextMode,
    SetCharset,
    SetCsmp,
    SetStorage,
    SendHeader,
    WritePayload,
    WaitAck,
    ListUnread,
    ReadMessage,
    DeleteIndex,
    DeleteRead,
    Done,
};

const char* to_string(SmsStep s);

class SmsService final : public IService
{
  public:
    using SendDone = Delegate<const SmsSendReport&>;
    using Received = Delegate<const SmsMessage&>;

    SmsService(AtEngine& engine, SessionState& session, const DeviceTraits& traits)
        : at_(engine), session_(session), traits_(traits)
    {
    }

    bool start_send(const char* number_e164, const char* utf8_text,
                    SmsEncoding enc = SmsEncoding::Auto);
    bool start_read(int16_t storage_index, bool delete_after = true);
    bool start_scan_unread();
    bool start_delete_index(int16_t storage_index);
    bool start_delete_read();

    bool            active() const override { return step_ != SmsStep::Idle; }
    void            pump() override;
    void            cancel() override;
    const char*     step_name() const override { return to_string(step_); }
    const AtResult& result() const override { return result_; }

    void on_line(const char* line, uint16_t len) override;
    void on_prompt() override;
    void on_final(const AtResult& res) override;

    const SmsSendReport& send_report() const { return report_; }
    const SmsMessage&    last_message() const { return last_; }
    void                 set_on_received(Received cb) { on_received_ = cb; }
    void                 set_default_encoding(SmsEncoding enc) { default_enc_ = enc; }
    void                 set_delete_after_read(bool on) { delete_after_ = on; }
    uint8_t              pending_indices() const { return pending_count_; }

    // Strict outgoing length guard (UTF-8 characters, 0 disables the guard).
    // The default (GSM_SMS_MAX_CHARS) sits safely below one UCS2 SMS (70 chars)
    // because the M08R family is known to hang / emit empty SMS on long payloads.
    void            set_max_chars(uint16_t n) { max_chars_ = n; }
    uint16_t        max_chars() const { return max_chars_; }
    static uint16_t count_chars(const char* utf8);

  private:
    void goto_step(SmsStep s)
    {
        step_   = s;
        issued_ = false;
    }
    void advance_step();
    void fail(AtStatus st, int16_t code);
    void complete();
    bool wants_ucs2() const;
    void parse_cmgr_header(const char* line);
    void store_body(const char* line);
    void queue_index(int16_t index);

    AtEngine&           at_;
    SessionState&       session_;
    const DeviceTraits& traits_;

    SmsStep     step_                = SmsStep::Idle;
    bool        issued_              = false;
    bool        prompt_seen_         = false;
    bool        deleting_after_read_ = false;
    bool        delete_after_        = true;
    SmsEncoding default_enc_         = SmsEncoding::Ucs2;
    SmsEncoding active_enc_          = SmsEncoding::Ucs2;
    uint16_t    max_chars_           = GSM_SMS_MAX_CHARS;

    PhoneNumber                      recipient_;
    StaticString<GSM_SMS_TEXT_BYTES> outgoing_;
    int16_t                          target_index_ = -1;
    int16_t                          pending_[GSM_SMS_INBOX_SLOTS]{};
    uint8_t                          pending_count_ = 0;

    SmsMessage    last_;
    SmsSendReport report_;
    AtResult      result_;
    Received      on_received_;
    bool          expect_body_ = false;
};

} // namespace gsm
