#include "gsm/log/log.hpp"
#include <cstring>

namespace gsm::log
{

Sink      Registry::sink_       = &stdio_sink;
void*     Registry::ctx_        = nullptr;
ClockHook Registry::clock_      = nullptr;
Level     Registry::global_     = Level::Trace;
bool      Registry::color_      = (GSM_LOG_COLOR_DEFAULT != 0);
bool      Registry::timestamps_ = true;

void Registry::set_sink(Sink sink, void* ctx)
{
    sink_ = sink ? sink : &stdio_sink;
    ctx_  = ctx;
}

void Registry::emit(const Record& rec)
{
    if (sink_) sink_(rec, ctx_);
}

const char* Registry::level_name(Level l)
{
    switch (l)
    {
    case Level::Error:
        return "E";
    case Level::Warn:
        return "W";
    case Level::Info:
        return "I";
    case Level::Debug:
        return "D";
    case Level::Trace:
        return "T";
    default:
        return "-";
    }
}

const char* Registry::level_color(Level l)
{
    switch (l)
    {
    case Level::Error:
        return "\033[1;31m";
    case Level::Warn:
        return "\033[1;33m";
    case Level::Info:
        return "\033[1;36m";
    case Level::Debug:
        return "\033[0;37m";
    case Level::Trace:
        return "\033[2m";
    default:
        return "";
    }
}

void Channel::vlog(Level level, const char* fmt, va_list ap) const
{
    if (!wants(level)) return;
    char           buf[GSM_LOG_BUFFER_SIZE];
    const int      n = vsnprintf(buf, sizeof(buf), fmt, ap);
    const uint16_t len =
        n < 0 ? 0
              : (n >= static_cast<int>(sizeof(buf)) ? static_cast<uint16_t>(sizeof(buf) - 1)
                                                    : static_cast<uint16_t>(n));
    Record rec{level, tag_, buf, len, Registry::now_ms()};
    Registry::emit(rec);
}

#define GSM_LOG_IMPL(name, lvl)                                                                    \
    void Channel::name(const char* fmt, ...) const                                                 \
    {                                                                                              \
        if (!wants(lvl)) return;                                                                   \
        va_list ap;                                                                                \
        va_start(ap, fmt);                                                                         \
        vlog(lvl, fmt, ap);                                                                        \
        va_end(ap);                                                                                \
    }

GSM_LOG_IMPL(error, Level::Error)
GSM_LOG_IMPL(warn, Level::Warn)
GSM_LOG_IMPL(info, Level::Info)
GSM_LOG_IMPL(debug, Level::Debug)
GSM_LOG_IMPL(trace, Level::Trace)
#undef GSM_LOG_IMPL

void Channel::bytes(Level level, const char* prefix, const uint8_t* data, uint16_t len) const
{
    if (!wants(level)) return;
    char        buf[GSM_LOG_BUFFER_SIZE];
    uint16_t    w = 0;
    const char* p = prefix ? prefix : "";
    while (*p && w + 1u < sizeof(buf))
        buf[w++] = *p++;
    for (uint16_t i = 0; i < len && w + 8u < sizeof(buf); ++i)
    {
        const uint8_t c = data[i];
        if (c == '\r')
        {
            buf[w++] = '\\';
            buf[w++] = 'r';
        }
        else if (c == '\n')
        {
            buf[w++] = '\\';
            buf[w++] = 'n';
        }
        else if (c == 0x1A) { w += snprintf(buf + w, sizeof(buf) - w, "<CtrlZ>"); }
        else if (c == 0x1B) { w += snprintf(buf + w, sizeof(buf) - w, "<ESC>"); }
        else if (c >= 0x20 && c < 0x7F) { buf[w++] = static_cast<char>(c); }
        else
        {
            w += snprintf(buf + w, sizeof(buf) - w, "<%02X>", c);
        }
    }
    buf[w] = '\0';
    Record rec{level, tag_, buf, w, Registry::now_ms()};
    Registry::emit(rec);
}

void Channel::hexdump(Level level, const char* prefix, const uint8_t* data, uint16_t len) const
{
    if (!wants(level)) return;
    char     buf[GSM_LOG_BUFFER_SIZE];
    uint16_t off = 0;
    while (off < len)
    {
        int w = snprintf(buf, sizeof(buf), "%s%04X:", prefix ? prefix : "", off);
        for (uint16_t i = 0; i < 16 && off + i < len; ++i)
            w += snprintf(buf + w, sizeof(buf) - w, " %02X", data[off + i]);
        Record rec{level, tag_, buf, static_cast<uint16_t>(w), Registry::now_ms()};
        Registry::emit(rec);
        off = static_cast<uint16_t>(off + 16);
    }
}

ScopedSink::ScopedSink(Sink sink, void* ctx)
    : prev_sink_(Registry::sink()), prev_ctx_(Registry::ctx())
{ Registry::set_sink(sink, ctx); }
ScopedSink::~ScopedSink()
{ Registry::set_sink(prev_sink_, prev_ctx_); }

void stdio_sink(const Record& rec, void*)
{
    const bool color = Registry::color();
    if (Registry::timestamps())
        printf("%s[%8lu][%s/%s]%s %s\r\n", color ? Registry::level_color(rec.level) : "",
               static_cast<unsigned long>(rec.timestamp_ms), Registry::level_name(rec.level),
               rec.tag, color ? "\033[0m" : "", rec.text);
    else
        printf("%s[%s/%s]%s %s\r\n", color ? Registry::level_color(rec.level) : "",
               Registry::level_name(rec.level), rec.tag, color ? "\033[0m" : "", rec.text);
}

void null_sink(const Record&, void*) {}

void writer_sink(const Record& rec, void* ctx)
{
    auto* w = static_cast<WriterSink*>(ctx);
    if (!w || !w->write) return;
    char       line[GSM_LOG_BUFFER_SIZE + 64];
    const bool color = Registry::color();
    int        n     = 0;
    if (Registry::timestamps())
    {
        n = snprintf(line, sizeof(line), "%s[%8lu][%s/%s]%s %s\r\n",
                     color ? Registry::level_color(rec.level) : "",
                     static_cast<unsigned long>(rec.timestamp_ms), Registry::level_name(rec.level),
                     rec.tag, color ? "\033[0m" : "", rec.text);
    }
    else
    {
        n = snprintf(line, sizeof(line), "%s[%s/%s]%s %s\r\n",
                     color ? Registry::level_color(rec.level) : "", Registry::level_name(rec.level),
                     rec.tag, color ? "\033[0m" : "", rec.text);
    }
    if (n > 0) w->write(w->ctx, reinterpret_cast<const uint8_t*>(line), static_cast<uint16_t>(n));
}

} // namespace gsm::log
