#include "gsm/cli/cli.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gsm::cli
{

// --- TextSink ---------------------------------------------------------------

void TextSink::put(const char* s) const
{
    if (!write || !s) return;
    write(ud, s, static_cast<uint16_t>(std::strlen(s)));
}

void TextSink::printf(const char* fmt, ...) const
{
    if (!write) return;
    char    buf[192];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        write(ud, buf, static_cast<uint16_t>(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

// --- ANSI palette (kept tiny on purpose) ------------------------------------

namespace
{
constexpr const char* RST       = "\033[0m";
constexpr const char* BOLD      = "\033[1m";
constexpr const char* DIM       = "\033[2m";
constexpr const char* RED       = "\033[31m";
constexpr const char* GREEN     = "\033[32m";
constexpr const char* YELLOW    = "\033[33m";
constexpr const char* BLUE      = "\033[34m";
constexpr const char* MAGENTA   = "\033[35m";
constexpr const char* CYAN      = "\033[36m";
constexpr const char* WHITE     = "\033[37m";
constexpr const char* B_RED     = "\033[1;31m";
constexpr const char* B_GREEN   = "\033[1;32m";
constexpr const char* B_YELLOW  = "\033[1;33m";
constexpr const char* B_BLUE    = "\033[1;34m";
constexpr const char* B_MAGENTA = "\033[1;35m";
constexpr const char* B_CYAN    = "\033[1;36m";
constexpr const char* B_WHITE   = "\033[1;37m";
constexpr const char* CLR       = "\033[2J";
constexpr const char* HOME      = "\033[H";

const char* lvl_name(log::Level l)
{
    switch (l)
    {
    case log::Level::Trace:
        return "TRACE";
    case log::Level::Debug:
        return "DEBUG";
    case log::Level::Info:
        return "INFO";
    case log::Level::Warn:
        return "WARN";
    case log::Level::Error:
        return "ERROR";
    default:
        return "?";
    }
}

const char* lvl_color(log::Level l)
{
    switch (l)
    {
    case log::Level::Trace:
        return DIM;
    case log::Level::Debug:
        return CYAN;
    case log::Level::Info:
        return B_GREEN;
    case log::Level::Warn:
        return B_YELLOW;
    case log::Level::Error:
        return B_RED;
    default:
        return WHITE;
    }
}
} // namespace

// --- output plumbing --------------------------------------------------------

void Console::out(const char* s) const
{
    if (!s) return;
    if (web_mode_)
    {
        web_out_chunk(s);
        return;
    }
    out_.put(s);
}

void Console::outf(const char* fmt, ...) const
{
    char    buf[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out(buf);
}

void Console::web_out_chunk(const char* s) const
{
    if (!web_write_ || !s) return;
    for (; *s; ++s)
    {
        if (*s == '\n')
        {
            if (wlen_ > 0)
            {
                wline_[wlen_] = '\0';
                web_write_(wline_, web_ud_);
                wlen_ = 0;
            }
            continue;
        }
        if (*s == '\r') continue;
        if (wlen_ + 1u >= sizeof(wline_))
        {
            wline_[wlen_] = '\0';
            web_write_(wline_, web_ud_);
            wlen_ = 0;
        }
        wline_[wlen_++] = *s;
    }
}

void Console::web_event(const char* fmt, ...) const
{
    if (!web_write_ || web_mode_) return;
    char    buf[176];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    web_out_chunk(buf);
    web_out_chunk("\n");
}

void Console::set_web_output(void (*out_fn)(const char*, void*), void* ud)
{
    web_write_ = out_fn;
    web_ud_    = ud;
}

void Console::log_thunk(void* ctx, const uint8_t* data, uint16_t len)
{
    auto* self = static_cast<Console*>(ctx);
    if (!self) return;
    if (self->out_.write) self->out_.write(self->out_.ud, reinterpret_cast<const char*>(data), len);
    // mirror the same bytes to the web console, chunked on newlines
    if (self->web_write_)
    {
        char     buf[96];
        uint16_t take = (len < sizeof(buf)) ? len : (uint16_t)(sizeof(buf) - 1u);
        std::memcpy(buf, data, take);
        buf[take] = '\0';
        self->web_out_chunk(buf);
    }
}

void Console::install_log_mirror()
{
    log_sink_.ctx = this;
    log::Registry::set_sink(&log::writer_sink, &log_sink_);
    log::Registry::set_color(true);
    log::Registry::set_timestamps(false);
    log::Registry::set_global_level(log::Level::Warn);
}

// --- ANSI helpers ------------------------------------------------------------

void Console::ansi_bar(int pct, int w) const
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const int   n   = (pct * w) / 100;
    const char* col = (pct < 30) ? B_RED : (pct < 60 ? B_YELLOW : B_GREEN);
    out(col);
    for (int i = 0; i < w; ++i)
    {
        if (i < n)
            out("#");
        else
        {
            out(DIM);
            out(".");
            out(RST);
            out(col);
        }
    }
    out(RST);
}

void Console::ansi_divider() const
{
    out(DIM);
    out("  --------------------------------------------------------\r\n");
    out(RST);
}

void Console::ansi_clear() const
{
    out(CLR);
    out(HOME);
}

void Console::kv(const char* key, const char* val, const char* vcol) const
{
    out("  ");
    out(B_WHITE);
    out(key);
    out(RST);
    for (int i = (int)std::strlen(key); i < 14; ++i)
        out(" ");
    out(" : ");
    out(vcol);
    out(val);
    out(RST);
    out("\r\n");
}

void Console::prompt()
{
    out(B_BLUE);
    out("gsm> ");
    out(RST);
}

// --- views -------------------------------------------------------------------

void Console::draw_status_bar()
{
    const auto& st = modem_.status();
    char        buf[48];

    out("  ");
    if (st.network_ready)
    {
        out(B_GREEN);
        out("NET");
        out(RST);
        out(":");
        out(st.operator_name.empty() ? "?" : st.operator_name.c_str());
    }
    else
    {
        out(B_RED);
        out("NET:--");
        out(RST);
    }

    out("  ");
    out(B_WHITE);
    out("SIG");
    out(RST);
    out(":");
    std::snprintf(buf, sizeof(buf), "%ddBm[%d/5]", st.rssi_dbm, st.signal_bars());
    out(buf);

    out("  ");
    out(st.sim_ready ? B_GREEN : B_RED);
    out(st.sim_ready ? "SIM:OK" : "SIM:--");
    out(RST);

    out("  ");
    out(B_WHITE);
    out("SMS");
    out(RST);
    out(":");
    std::snprintf(buf, sizeof(buf), "%u/%u", st.sms_used, st.sms_total);
    out(buf);

    out("  ");
    const bool paused = modem_.queue_paused();
    out(paused ? B_YELLOW : B_GREEN);
    std::snprintf(buf, sizeof(buf), "Q:%s%u/%u", paused ? "!" : "", modem_.queue_size(),
                  JobQueue::capacity());
    out(buf);
    out(RST);

    out("  ");
    const char* cstate = to_string(modem_.call().state());
    if (std::strcmp(cstate, "idle") != 0)
    {
        out(B_YELLOW);
        out("CALL:");
        out(cstate);
        out(RST);
        out("  ");
    }

    out(DIM);
    out("FSM:");
    out(modem_.state_name());
    out(RST);
    out("\r\n");
}

void Console::draw_live_status()
{
    ansi_clear();
    const auto& st = modem_.status();

    out("\r\n  ");
    out(B_MAGENTA);
    out("\xe2\x97\x89 LIVE MODEM STATUS"); // ◉
    out(DIM);
    out("  (ESC to exit, type commands below)");
    out(RST);
    out("\r\n");
    ansi_divider();

    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s (%s/%s)", modem_.state_name(), modem_.activity(),
                  modem_.step());
    kv("FSM", buf, modem_.ready() ? B_GREEN : B_YELLOW);

    const int bars = st.signal_bars();
    const int pct  = (bars * 100) / 5;
    out("  ");
    out(B_WHITE);
    out("Signal");
    for (int i = 6; i < 14; ++i)
        out(" ");
    out(RST);
    out(" : ");
    ansi_bar(pct, 20);
    std::snprintf(buf, sizeof(buf), "  %3ddBm [%d/5]", st.rssi_dbm, bars);
    out(buf);
    out("\r\n");

    if (st.network_ready)
    {
        std::snprintf(buf, sizeof(buf), "REGISTERED (%s)", to_string(st.registration));
        kv("Network", buf, B_GREEN);
    }
    else
    {
        kv("Network", "NO SIGNAL / NOT REGISTERED", B_RED);
    }

    kv("Operator", st.operator_name.empty() ? "N/A" : st.operator_name.c_str(), B_CYAN);
    kv("SIM", st.sim_ready ? "READY" : to_string(st.sim_state), st.sim_ready ? B_GREEN : B_RED);

    const int spct = st.sms_total ? (st.sms_used * 100) / st.sms_total : 0;
    out("  ");
    out(B_WHITE);
    out("SMS Storage");
    out(RST);
    out(" : ");
    ansi_bar(spct, 20);
    std::snprintf(buf, sizeof(buf), "  %u/%u msgs", st.sms_used, st.sms_total);
    out(buf);
    out("\r\n");

    const auto* peer = modem_.call().peer();
    if (peer && *peer)
    {
        std::snprintf(buf, sizeof(buf), "%s -> %s", to_string(modem_.call().state()), peer);
        kv("Call", buf, B_YELLOW);
        std::snprintf(buf, sizeof(buf), "end-reason=%s",
                      to_string(modem_.call().last_end_reason()));
        kv("Call End", buf, DIM);
    }
    else
    {
        kv("Call", to_string(modem_.call().state()), DIM);
    }

    std::snprintf(buf, sizeof(buf), "%s (%u/%u)", modem_.queue_paused() ? "PAUSED" : "ACTIVE",
                  modem_.queue_size(), JobQueue::capacity());
    kv("Queue", buf, modem_.queue_paused() ? B_YELLOW : B_GREEN);

    const auto& wd = modem_.maintenance();
    std::snprintf(buf, sizeof(buf), "resets=%lu ops=%lu/%u", (unsigned long)wd.resets_done(),
                  (unsigned long)wd.ops_count(), wd.policy().ops_period);
    kv("Watchdog", buf, wd.policy().enabled ? GREEN : DIM);
    std::snprintf(buf, sizeof(buf), "health=%s rssi=%ddBm", to_string(wd.health().state),
                  wd.health().rssi_dbm);
    kv("Health", buf, wd.health().ok() ? B_GREEN : B_YELLOW);

    const uint32_t age = st.updated_at_ms ? (clock_.millis() - st.updated_at_ms) : 0;
    if (age < 1000)
        std::snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)age);
    else
        std::snprintf(buf, sizeof(buf), "%lu.%01lu s", (unsigned long)(age / 1000),
                      (unsigned long)((age % 1000) / 100));
    kv("Updated", buf, DIM);

    if (show_rate_)
    {
        const uint32_t span = clock_.millis() - loop_mark_ms_;
        const uint32_t rate = span ? (loop_count_ * 1000) / span : 0;
        loop_count_         = 0;
        loop_mark_ms_       = clock_.millis();
        std::snprintf(buf, sizeof(buf), "%lu/loop", (unsigned long)rate);
        kv("Rate", buf, DIM);
    }

    ansi_divider();
    prompt();
}

void Console::draw_queue()
{
    ansi_clear();
    out("\r\n  ");
    out(B_MAGENTA);
    out("\xe2\x97\x89 QUEUE STATUS");
    out(DIM);
    out("  (press ESC to exit)");
    out(RST);
    out("\r\n");
    ansi_divider();

    const bool paused = modem_.queue_paused();
    char       buf[64];
    std::snprintf(buf, sizeof(buf), "%s", paused ? "PAUSED" : "ACTIVE");
    kv("State", buf, paused ? B_RED : B_GREEN);

    std::snprintf(buf, sizeof(buf), "%u / %u", modem_.queue_size(), JobQueue::capacity());
    kv("Size", buf, B_YELLOW);

    const auto& cur = modem_.current_job();
    if (cur.kind != JobKind::None)
    {
        std::snprintf(buf, sizeof(buf), "#%u %s (try %u/%u)%s", cur.id, to_string(cur.kind),
                      cur.attempts, cur.max_attempts, cur.deferred ? " [held-for-call]" : "");
        kv("Current", buf, B_CYAN);
    }
    else
    {
        kv("Current", "(idle)", DIM);
    }

    out("\r\n  ");
    out(B_WHITE);
    out("Queued Jobs:");
    out(RST);
    out("\r\n");

    for (uint8_t i = 0; i < modem_.queue_size(); ++i)
    {
        const auto* j = modem_.queued(i);
        if (!j) continue;
        out("    [");
        out(DIM);
        char idx[8];
        std::snprintf(idx, sizeof(idx), "%u", i);
        out(idx);
        out(RST);
        out("] #");
        char id[8];
        std::snprintf(id, sizeof(id), "%u", j->id);
        out(id);
        out("  ");
        out(B_CYAN);
        out(to_string(j->kind));
        out(RST);
        if (j->urgent)
        {
            out("  ");
            out(B_RED);
            out("URGENT");
            out(RST);
        }
        out("\r\n");
    }

    ansi_divider();
    prompt();
}

void Console::draw_watchdog()
{
    const auto& wd  = modem_.maintenance();
    const auto& pol = wd.policy();
    char        buf[80];

    out("\r\n  ");
    out(B_MAGENTA);
    out("\xe2\x97\x89 MAINTENANCE WATCHDOG");
    out(RST);
    out("\r\n");
    ansi_divider();

    kv("Enabled", pol.enabled ? "YES" : "NO", pol.enabled ? B_GREEN : B_RED);
    std::snprintf(buf, sizeof(buf), "%lu ms (%.1f min)%s", (unsigned long)pol.periodic_ms,
                  pol.periodic_ms / 60000.0, pol.periodic_ms ? "" : " [off]");
    kv("Reset #1 time", buf, B_CYAN);
    std::snprintf(buf, sizeof(buf), "%u ops%s", pol.ops_period, pol.ops_period ? "" : " [off]");
    kv("Reset #2 ops", buf, B_CYAN);
    std::snprintf(buf, sizeof(buf), "%lu ms", (unsigned long)pol.grace_ms);
    kv("Grace", buf, DIM);
    std::snprintf(buf, sizeof(buf), "%s",
                  pol.use_power_cycle ? "PWRKEY power-cycle" : "soft reset");
    kv("Reset mode", buf, DIM);

    std::snprintf(buf, sizeof(buf), "%lu resets, uptime %lu ms", (unsigned long)wd.resets_done(),
                  (unsigned long)wd.since_reset_ms(clock_.millis()));
    kv("Counters", buf, B_YELLOW);
    std::snprintf(buf, sizeof(buf), "%lu / %u ops", (unsigned long)wd.ops_count(), pol.ops_period);
    kv("Ops budget", buf, B_YELLOW);

    std::snprintf(buf, sizeof(buf), "%s", to_string(wd.health().state));
    kv("Health", buf, wd.health().ok() ? B_GREEN : B_YELLOW);
    std::snprintf(buf, sizeof(buf), "at=%d sim=%d net=%d rssi=%ddBm", (int)wd.health().at_ok,
                  (int)wd.health().sim_ok, (int)wd.health().net_ok, wd.health().rssi_dbm);
    kv("Health detail", buf, DIM);

    ansi_divider();
    prompt();
}

void Console::draw_menu()
{
    ansi_clear();

    out("\r\n  ");
    out(B_MAGENTA);
    out("\xe2\x97\x89 GSM MODEM INDUSTRIAL CONSOLE \xe2\x97\x89");
    out(RST);
    out("\r\n");

    draw_status_bar();
    ansi_divider();

    out("  ");
    out(B_CYAN);
    out("[STATUS & INFO]");
    out(RST);
    out("\r\n    ");
    out(B_YELLOW);
    out("i");
    out(RST);
    out("  Live status (auto-refresh)     ");
    out(B_YELLOW);
    out("s");
    out(RST);
    out("  Trigger sweep\r\n    ");
    out(B_YELLOW);
    out("q");
    out(RST);
    out("  Queue details                  ");
    out(B_YELLOW);
    out("r");
    out(RST);
    out("  Scan unread SMS\r\n    ");
    out(B_YELLOW);
    out("d");
    out(RST);
    out("  Clean read SMS                 ");
    out(B_YELLOW);
    out("b");
    out(RST);
    out("  SIM balance (USSD)\r\n    ");
    out(B_YELLOW);
    out("w");
    out(RST);
    out("  Watchdog & health panel        ");
    out(B_YELLOW);
    out("health");
    out(RST);
    out("  Run health check\r\n");

    out("\r\n  ");
    out(B_GREEN);
    out("[ACTIONS & RECOVERY]");
    out(RST);
    out("\r\n    ");
    out(B_YELLOW);
    out("p");
    out(RST);
    out("  Pause/Resume queue             ");
    out(B_YELLOW);
    out("x");
    out(RST);
    out("  Stop & clear queue\r\n    ");
    out(B_YELLOW);
    out("h");
    out(RST);
    out("  Urgent hangup                  ");
    out(B_YELLOW);
    out("R");
    out(RST);
    out("  Reset FSM (Software)\r\n    ");
    out(B_YELLOW);
    out("K");
    out(RST);
    out("  Pulse PWRKEY (Hard Reset)      ");
    out(B_YELLOW);
    out("P");
    out(RST);
    out("  Passthrough bridge\r\n    ");
    out(B_YELLOW);
    out("c");
    out(RST);
    out("  Clear screen\r\n");

    out("\r\n  ");
    out(B_YELLOW);
    out("[POLICY]");
    out(RST);
    out("\r\n    ");
    out(DIM);
    out("smsmax <chars>   callto <ms>   wreset\r\n");

    out("\r\n  ");
    out(B_YELLOW);
    out("[LOGGING]");
    out(RST);
    out("  level=");
    out(lvl_color(log::Registry::global_level()));
    out(lvl_name(log::Registry::global_level()));
    out(RST);
    out("\r\n    ");
    out(B_YELLOW);
    out("L");
    out(RST);
    out("  Cycle log level                ");
    out(B_YELLOW);
    out("T");
    out(RST);
    out("  Toggle timestamps\r\n    ");
    out(B_YELLOW);
    out("C");
    out(RST);
    out("  Toggle ANSI colors\r\n");

    out("\r\n  ");
    out(B_BLUE);
    out("[COMMANDS]");
    out(RST);
    out("\r\n    ");
    out(DIM);
    out("sms <num> <text>    dial <num>    ussd <code>\r\n");
    out("    read <idx>          del <idx>     at <cmd>");
    out(RST);
    out("\r\n");

    ansi_divider();
    out("  ");
    out(B_YELLOW);
    out("m");
    out(RST);
    out(" - Reprint menu\r\n");
    prompt();
}

void Console::redraw_current()
{
    if (web_mode_) return; // full-screen repaints are serial-only

    if (mode_ == Mode::LiveStatus)
    {
        out("\r\n");
        draw_live_status();
    }
    // keep quiet in menu mode: log stream owns the screen
}

void Console::redraw()
{
    if (mode_ == Mode::LiveStatus)
        draw_live_status();
    else if (mode_ == Mode::Menu)
        draw_menu();
}

// --- command handling --------------------------------------------------------

void Console::cycle_level()
{
    log::Level l = log::Registry::global_level();
    switch (l)
    {
    case log::Level::Trace:
        l = log::Level::Debug;
        break;
    case log::Level::Debug:
        l = log::Level::Info;
        break;
    case log::Level::Info:
        l = log::Level::Warn;
        break;
    case log::Level::Warn:
        l = log::Level::Error;
        break;
    case log::Level::Error:
        l = log::Level::Trace;
        break;
    default:
        l = log::Level::Info;
        break;
    }
    log::Registry::set_global_level(l);
}

void Console::handle(char* line)
{
    while (*line == ' ')
        ++line;
    if (!*line)
    {
        redraw_current();
        return;
    }

    // these commands redraw the serial terminal only
    if (web_mode_ && (std::strcmp(line, "m") == 0 || std::strcmp(line, "c") == 0))
    {
        outf("menu view is on the device screen / serial console\r\n");
        return;
    }
    if (web_mode_ && std::strcmp(line, "i") == 0)
    {
        outf("live status is shown in the GSM tab of the web panel\r\n");
        return;
    }
    if (web_mode_ && std::strcmp(line, "P") == 0)
    {
        outf("passthrough mode is only available on the device serial console\r\n");
        return;
    }

    if (std::strcmp(line, "m") == 0)
    {
        mode_ = Mode::Menu;
        draw_menu();
        return;
    }
    if (std::strcmp(line, "c") == 0)
    {
        mode_ = Mode::Menu;
        draw_menu();
        return;
    }
    if (std::strcmp(line, "i") == 0)
    {
        mode_ = Mode::LiveStatus;
        draw_live_status();
        return;
    }
    if (std::strcmp(line, "q") == 0)
    {
        draw_queue();
        return;
    }
    if (std::strcmp(line, "w") == 0)
    {
        draw_watchdog();
        return;
    }
    if (std::strcmp(line, "health") == 0)
    {
        outf("Running health check...\r\n");
        modem_.maintenance().start_check();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "wreset") == 0)
    {
        outf("%sMaintenance reset requested.%s\r\n", B_YELLOW, RST);
        modem_.maintenance().request_reset(ResetReason::Manual);
        redraw_current();
        return;
    }

    if (std::strcmp(line, "s") == 0)
    {
        outf("Triggering sweep...\r\n");
        modem_.refresh_status();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "r") == 0)
    {
        outf("Scanning SMS...\r\n");
        modem_.scan_sms();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "d") == 0)
    {
        outf("Deleting read SMS...\r\n");
        modem_.delete_read_sms();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "h") == 0)
    {
        outf("%sHangup queued.%s\r\n", B_YELLOW, RST);
        modem_.hang_up();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "b") == 0)
    {
        outf("Querying balance...\r\n");
        modem_.check_balance();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "p") == 0)
    {
        if (modem_.queue_paused())
        {
            modem_.resume_queue();
            outf("%sResumed.%s\r\n", B_GREEN, RST);
        }
        else
        {
            modem_.pause_queue();
            outf("%sPaused.%s\r\n", B_YELLOW, RST);
        }
        redraw_current();
        return;
    }
    if (std::strcmp(line, "x") == 0)
    {
        outf("%sStopping all and clearing queue!%s\r\n", B_RED, RST);
        modem_.stop_all(false);
        redraw_current();
        return;
    }
    if (std::strcmp(line, "R") == 0)
    {
        outf("%sSoftware reset of FSM...%s\r\n", B_YELLOW, RST);
        modem_.reset();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "K") == 0)
    {
        outf("%sHardware Power Cycle (PWRKEY)...%s\r\n", B_MAGENTA, RST);
        modem_.power_cycle();
        redraw_current();
        return;
    }
    if (std::strcmp(line, "P") == 0)
    {
        set_passthrough(!passthrough_);
        if (passthrough_)
            outf("%sPassthrough ON%s (type 'e' to exit)\r\n", B_MAGENTA, RST);
        else
            outf("%sPassthrough OFF%s\r\n", YELLOW, RST);
        return;
    }

    if (std::strcmp(line, "L") == 0)
    {
        cycle_level();
        outf("Log level: %s%s%s\r\n", lvl_color(log::Registry::global_level()),
             lvl_name(log::Registry::global_level()), RST);
        redraw_current();
        return;
    }
    if (std::strcmp(line, "T") == 0)
    {
        const bool n = !log::Registry::timestamps();
        log::Registry::set_timestamps(n);
        outf("Timestamps %s\r\n", n ? "on" : "off");
        redraw_current();
        return;
    }
    if (std::strcmp(line, "C") == 0)
    {
        const bool n = !log::Registry::color();
        log::Registry::set_color(n);
        outf("ANSI %s\r\n", n ? "on" : "off");
        redraw_current();
        return;
    }

    if (std::strncmp(line, "smsmax ", 7) == 0)
    {
        const int n = std::atoi(line + 7);
        modem_.sms().set_max_chars(static_cast<uint16_t>(n < 0 ? 0 : n));
        outf("SMS max chars = %u\r\n", modem_.sms().max_chars());
        redraw_current();
        return;
    }
    if (std::strncmp(line, "callto ", 7) == 0)
    {
        const long ms                        = std::atol(line + 7);
        modem_.call_policy().dial_timeout_ms = static_cast<uint32_t>(ms > 0 ? ms : 0);
        modem_.call_policy().ring_timeout_ms = static_cast<uint32_t>(ms > 0 ? ms : 0);
        outf("Call auto-hangup timeout = %lu ms\r\n",
             (unsigned long)modem_.call_policy().dial_timeout_ms);
        redraw_current();
        return;
    }

    if (std::strncmp(line, "sms ", 4) == 0)
    {
        char* p = line + 4;
        while (*p == ' ')
            ++p;
        char* num = p;
        char* sep = std::strchr(p, ' ');
        if (!sep)
        {
            outf("%sUsage: sms <num> <text>%s\r\n", B_RED, RST);
            redraw_current();
            return;
        }
        *sep       = '\0';
        char* text = sep + 1;
        while (*text == ' ')
            ++text;
        JobId id = modem_.send_sms(num, text);
        if (id != kInvalidJob)
            outf("%sQueued SMS #%u to %s%s\r\n", B_GREEN, id, num, RST);
        else
            outf("%sRejected (queue full or text too long, see log)%s\r\n", B_RED, RST);
        redraw_current();
        return;
    }
    if (std::strncmp(line, "dial ", 5) == 0)
    {
        char* num = line + 5;
        while (*num == ' ')
            ++num;
        JobId id = modem_.dial(num);
        if (id != kInvalidJob)
            outf("%sDial #%u to %s%s\r\n", B_GREEN, id, num, RST);
        else
            outf("%sQueue full%s\r\n", B_RED, RST);
        redraw_current();
        return;
    }
    if (std::strncmp(line, "ussd ", 5) == 0)
    {
        char* code = line + 5;
        while (*code == ' ')
            ++code;
        JobId id = modem_.ussd(code);
        outf("%sUSSD #%u (%s)%s\r\n", B_GREEN, id, code, RST);
        redraw_current();
        return;
    }
    if (std::strncmp(line, "read ", 5) == 0)
    {
        int idx = std::atoi(line + 5);
        modem_.read_sms(static_cast<int16_t>(idx));
        outf("Read SMS idx=%d\r\n", idx);
        redraw_current();
        return;
    }
    if (std::strncmp(line, "del ", 4) == 0)
    {
        int idx = std::atoi(line + 4);
        modem_.sms().start_delete_index(static_cast<int16_t>(idx));
        outf("Deleting idx=%d...\r\n", idx);
        redraw_current();
        return;
    }
    if (std::strncmp(line, "at ", 3) == 0)
    {
        char* at = line + 3;
        while (*at == ' ')
            ++at;
        if (modem_.at().idle())
        {
            outf("Sending: %s\r\n", at);
            modem_.at().execute(at, 5000, this);
        }
        else
        {
            outf("%sAT engine busy!%s\r\n", B_RED, RST);
        }
        redraw_current();
        return;
    }

    outf("%sUnknown: %s%s (press 'm')\r\n", B_RED, line, RST);
    redraw_current();
}

// --- modem event mirrors ------------------------------------------------------

void Console::on_sms_thunk(void* ctx, const SmsMessage& msg)
{
    auto* self = static_cast<Console*>(ctx);
    self->outf("%s[SMS]%s [%d] from=%s%s%s\r\n", B_GREEN, RST, msg.storage_index, B_YELLOW,
               msg.number.c_str(), RST);
    self->outf("  %s\r\n", msg.text.c_str());
    self->web_event("[SMS] [%d] from=%s", msg.storage_index, msg.number.c_str());
    self->web_event("  %s", msg.text.c_str());
    if (std::strcmp(msg.text.c_str(), "ping") == 0)
    {
        self->outf("Auto-reply 'pong'\r\n");
        self->web_event("auto-reply 'pong'");
        self->modem_.send_sms(msg.number.c_str(), "pong");
    }
    self->redraw_current();
}

void Console::on_call_thunk(void* ctx, CallState state, const char* peer)
{
    auto* self = static_cast<Console*>(ctx);
    self->outf("%s[CALL]%s %s%s%s peer=%s\r\n", B_YELLOW, RST, B_CYAN, to_string(state), RST,
               peer ? peer : "none");
    self->web_event("[CALL] %s peer=%s", to_string(state), peer ? peer : "none");
    self->redraw_current();
}

void Console::on_call_ended_thunk(void* ctx, CallEndReason reason)
{
    auto* self = static_cast<Console*>(ctx);
    self->outf("%s[CALL-END]%s %s\r\n", B_YELLOW, RST, to_string(reason));
    self->web_event("[CALL-END] %s", to_string(reason));
    self->redraw_current();
}

void Console::on_sim_thunk(void* ctx, SimState state)
{
    auto* self = static_cast<Console*>(ctx);
    if (state == SimState::Removed)
    {
        self->outf("%s[!] CRITICAL: SIM CARD REMOVED!%s\r\n", B_RED, RST);
        self->web_event("[!] CRITICAL: SIM CARD REMOVED!");
    }
    else if (state == SimState::Ready)
    {
        self->outf("%s[SIM] SIM CARD READY%s\r\n", B_GREEN, RST);
        self->web_event("[SIM] SIM CARD READY");
    }
    self->redraw_current();
}

void Console::on_job_thunk(void* ctx, const Job& job)
{
    auto* self = static_cast<Console*>(ctx);
    self->outf("\r\n%s[JOB#%u]%s %s %s%s%s code=%d\r\n", B_MAGENTA, job.id, RST,
               to_string(job.kind), job.state == JobState::Succeeded ? B_GREEN : B_RED,
               to_string(job.state), RST, job.result.code);
    if (job.kind == JobKind::UssdQuery && job.state == JobState::Succeeded)
        self->outf("  USSD: %s\r\n", job.ussd_response.c_str());
    self->web_event("[JOB#%u] %s %s code=%d", job.id, to_string(job.kind), to_string(job.state),
                    job.result.code);
    if (job.kind == JobKind::UssdQuery && job.state == JobState::Succeeded)
        self->web_event("USSD: %s", job.ussd_response.c_str());
    self->redraw_current();
}

void Console::on_health_thunk(void* ctx, const HealthReport& rep)
{
    auto* self = static_cast<Console*>(ctx);
    self->outf("%s[HEALTH]%s %s (at=%d sim=%d net=%d rssi=%ddBm)\r\n", rep.ok() ? B_GREEN : B_RED,
               RST, to_string(rep.state), (int)rep.at_ok, (int)rep.sim_ok, (int)rep.net_ok,
               rep.rssi_dbm);
    self->web_event("[HEALTH] %s at=%d sim=%d net=%d rssi=%d", to_string(rep.state), (int)rep.at_ok,
                    (int)rep.sim_ok, (int)rep.net_ok, rep.rssi_dbm);
}

// --- lifecycle -----------------------------------------------------------------

void Console::init(bool draw_menu_now)
{
    install_log_mirror();

    modem_.set_on_sms(SmsService::Received(&Console::on_sms_thunk, this));
    modem_.set_on_call(CallService::StateChanged(&Console::on_call_thunk, this));
    modem_.set_on_call_ended(CallService::Ended(&Console::on_call_ended_thunk, this));
    modem_.set_on_sim(Modem::SimEventCb(&Console::on_sim_thunk, this));
    modem_.set_on_job(JobCallback(&Console::on_job_thunk, this));
    modem_.set_on_health(MaintenanceWatchdog::ReportCb(&Console::on_health_thunk, this));

    loop_mark_ms_ = clock_.millis();
    if (draw_menu_now) draw_menu();
}

void Console::set_passthrough(bool on)
{
    passthrough_ = on;
    mode_        = on ? Mode::Passthrough : Mode::Menu;
}

void Console::feed(uint8_t byte)
{
    if (passthrough_)
    {
        if (byte == 'e')
        {
            set_passthrough(false);
            outf("\r\n%sExited passthrough.%s\r\n", B_MAGENTA, RST);
            draw_menu();
            return;
        }
        modem_.at().uart().write(&byte, 1);
        return;
    }

    if (byte == 0x1B) // ESC
    {
        if (mode_ != Mode::Menu)
        {
            mode_     = Mode::Menu;
            line_idx_ = 0;
            draw_menu();
        }
        return;
    }

    if (byte == '\r' || byte == '\n')
    {
        if (line_idx_ > 0)
        {
            line_[line_idx_] = '\0';
            out("\r\n");
            handle(line_);
            line_idx_ = 0;
        }
        else
        {
            out("\r\n");
            redraw_current();
        }
        return;
    }

    if (byte == 0x08 || byte == 0x7F)
    {
        if (line_idx_ > 0)
        {
            --line_idx_;
            out("\b \b");
        }
        return;
    }

    if (byte >= 0x20 && line_idx_ + 1u < sizeof(line_))
    {
        line_[line_idx_++] = static_cast<char>(byte);
        char s[2]          = {static_cast<char>(byte), 0};
        out(s);
    }
}

void Console::feed_line(const char* line)
{
    if (!line) return;
    char buf[sizeof(line_)];
    std::strncpy(buf, line, sizeof(buf) - 1u);
    buf[sizeof(buf) - 1u] = '\0';
    handle(buf);
}

void Console::exec_web(const char* line)
{
    if (!line || line[0] == '\0') return;
    if (!web_write_) return; // bridge not installed yet

    char buf[128];
    std::strncpy(buf, line, sizeof(buf) - 1u);
    buf[sizeof(buf) - 1u] = '\0';

    web_mode_ = true;
    handle(buf);
    web_out_chunk("\n"); // flush a trailing partial line
    web_mode_ = false;
}

void Console::status_line(char* dst, unsigned cap) const
{
    const auto& st   = modem_.status();
    const auto* peer = modem_.call().peer();

    std::snprintf(dst, cap, "%s|%s|%s|%u|%u|%u|%s|%d|%u|%u|%u|%u|%u|%u|%s|%s", modem_.state_name(),
                  modem_.activity(), modem_.step(), (unsigned)(modem_.ready() ? 1u : 0u),
                  (unsigned)(st.sim_ready ? 1u : 0u), (unsigned)(st.network_ready ? 1u : 0u),
                  st.operator_name.empty() ? "-" : st.operator_name.c_str(), (int)st.rssi_dbm,
                  (unsigned)st.signal_bars(), (unsigned)st.sms_used, (unsigned)st.sms_total,
                  (unsigned)(modem_.queue_paused() ? 1u : 0u), (unsigned)modem_.queue_size(),
                  (unsigned)JobQueue::capacity(), to_string(modem_.call().state()),
                  (peer && *peer) ? peer : "-");
}

// --- tick -----------------------------------------------------------------------

void Console::tick()
{
    ++loop_count_;

    if (passthrough_)
    {
        uint8_t  buf[64];
        uint16_t n = modem_.at().uart().read(buf, sizeof(buf));
        if (n && out_.write) out_.write(out_.ud, reinterpret_cast<const char*>(buf), n);
        return;
    }

    if (mode_ == Mode::LiveStatus && (clock_.millis() - last_draw_) >= 2000)
    {
        draw_live_status();
        last_draw_ = clock_.millis();
    }
}

void Console::on_line(const char* line, uint16_t len)
{
    (void)len;
    outf("%s\r\n", line);
}

void Console::on_final(const AtResult& res)
{ outf("<%s>\r\n", to_string(res.status)); }

} // namespace gsm::cli
