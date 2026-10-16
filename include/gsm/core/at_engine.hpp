#pragma once
#include "gsm/core/at_types.hpp"
#include "gsm/core/line_reader.hpp"
#include "gsm/core/urc_router.hpp"
#include "gsm/gsm_config.hpp"
#include "gsm/hal/hal.hpp"
#include "gsm/log/log.hpp"

namespace gsm
{

struct AtRequest
{
    const char* command       = nullptr;
    uint32_t    timeout_ms    = GSM_AT_DEFAULT_TIMEOUT;
    bool        expect_prompt = false;
    bool        echo_expected = true;
};

class AtEngine
{
  public:
    AtEngine(hal::IUart& uart, hal::IClock& clock, UrcRouter& urc)
        : uart_(uart), clock_(clock), urc_(urc)
    {
    }

    bool idle() const { return observer_ == nullptr; }
    bool busy() const { return observer_ != nullptr; }

    bool execute(const AtRequest& req, IAtObserver* observer);
    bool execute(const char* cmd, uint32_t timeout_ms, IAtObserver* observer);

    uint16_t write_raw(const uint8_t* data, uint16_t len);
    uint16_t write_raw(const char* text);
    void     write_ctrl_z()
    {
        const uint8_t b = 0x1A;
        write_raw(&b, 1);
    }
    void write_escape()
    {
        const uint8_t b = 0x1B;
        write_raw(&b, 1);
    }

    bool await_final(uint32_t timeout_ms, IAtObserver* observer);

    void abort();
    // Drop unread UART bytes and any partial line. Used at the start of a
    // hardware power-cycle so a leftover NORMAL POWER DOWN cannot surface
    // after the module has already come back up.
    void discard_rx();
    void tick();

    void        set_trace(bool on) { trace_ = on; }
    uint32_t    now() { return clock_.millis(); }
    hal::IUart& uart() { return uart_; }

  private:
    void finish(AtStatus status, int16_t code);
    bool classify_final(const char* line, AtStatus& status, int16_t& code) const;

    hal::IUart&               uart_;
    hal::IClock&              clock_;
    UrcRouter&                urc_;
    LineReader                reader_;
    IAtObserver*              observer_ = nullptr;
    StaticString<GSM_CMD_MAX> sent_;
    uint32_t                  started_       = 0;
    uint32_t                  timeout_       = 0;
    uint32_t                  last_done_     = 0;
    bool                      expect_prompt_ = false;
    bool                      trace_         = true;
};

} // namespace gsm
