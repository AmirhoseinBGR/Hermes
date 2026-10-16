#pragma once
#include "gsm/core/at_types.hpp"
#include "gsm/hal/hal.hpp"
#include "gsm/log/log.hpp"
#include "gsm/modem/modem.hpp"

namespace gsm::cli
{

// Portable byte sink: serial port, web console, test harness, ...
struct TextSink
{
    void (*write)(void* ud, const char* data, uint16_t len) = nullptr;
    void* ud                                                = nullptr;

    void put(const char* s) const;
    void printf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
};

// Interactive industrial console for the GSM library. This used to live in the
// application as "gsm_cli"; it is now a first-class library feature so any host
// (STM32, Arduino, host test) gets the same console by feeding bytes in.
//
//   gsm::cli::Console console(modem, clock);
//   console.set_output(sink);
//   console.init();          // registers callbacks, draws the menu
//   while (true) { console.tick(); ... console.feed(byte); }
//
// Command reference: docs/CLI.md (Persian) and docs/html/cli.html.
class Console final : public IAtObserver
{
  public:
    Console(Modem& modem, hal::IClock& clock) : modem_(modem), clock_(clock) {}

    void set_output(const TextSink& sink) { out_ = sink; }
    void set_web_output(void (*out)(const char*, void*), void* ud);
    // Route gsm::log records to the console output (and mirror to the web).
    void install_log_mirror();
    void set_loop_rate_display(bool on) { show_rate_ = on; }

    void init(bool draw_menu = true);
    void tick();
    void feed(uint8_t byte);                         // one byte of console input
    void feed_line(const char* line);                // one command line (no CR/LF needed)
    void exec_web(const char* line);                 // command from the web/serial bridge
    void status_line(char* dst, unsigned cap) const; // compact live status CSV
    void redraw();

    bool passthrough() const { return passthrough_; }
    void set_passthrough(bool on);

    // IAtObserver for the raw "at" command
    void on_line(const char* line, uint16_t len) override;
    void on_final(const AtResult& res) override;

  private:
    enum class Mode : uint8_t
    {
        Menu,
        LiveStatus,
        Passthrough
    };

    // rendering
    void out(const char* s) const;
    void outf(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void web_event(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void web_out_chunk(const char* s) const;
    void ansi_bar(int pct, int w) const;
    void ansi_divider() const;
    void ansi_clear() const;
    void kv(const char* key, const char* val, const char* vcol) const;
    void draw_menu();
    void draw_live_status();
    void draw_queue();
    void draw_status_bar();
    void draw_watchdog();
    void prompt();
    void redraw_current();

    // command handling
    void handle(char* line);
    void cycle_level();

    // modem event handlers (installed by init)
    static void on_sms_thunk(void* ctx, const SmsMessage& msg);
    static void on_call_thunk(void* ctx, CallState state, const char* peer);
    static void on_call_ended_thunk(void* ctx, CallEndReason reason);
    static void on_sim_thunk(void* ctx, SimState state);
    static void on_job_thunk(void* ctx, const Job& job);
    static void on_health_thunk(void* ctx, const HealthReport& rep);
    static void log_thunk(void* ctx, const uint8_t* data, uint16_t len);

    Modem&       modem_;
    hal::IClock& clock_;

    TextSink out_{};
    void (*web_write_)(const char*, void*) = nullptr;
    void*            web_ud_               = nullptr;
    bool             web_mode_             = false; // capturing for the web
    mutable char     wline_[192];
    mutable uint16_t wlen_ = 0;

    Mode     mode_ = Mode::Menu;
    char     line_[128];
    uint16_t line_idx_     = 0;
    uint32_t last_draw_    = 0;
    bool     show_rate_    = true;
    uint32_t loop_mark_ms_ = 0;
    uint32_t loop_count_   = 0;

    bool            passthrough_ = false;
    log::WriterSink log_sink_{&Console::log_thunk, nullptr};
};

} // namespace gsm::cli
