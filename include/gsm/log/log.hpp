#pragma once
#include "gsm/gsm_config.hpp"
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace gsm::log
{

enum class Level : uint8_t
{
    Off = 0,
    Error,
    Warn,
    Info,
    Debug,
    Trace
};

struct Record
{
    Level       level;
    const char* tag;
    const char* text;
    uint16_t    len;
    uint32_t    timestamp_ms;
};

using Sink      = void (*)(const Record& rec, void* ctx);
using ClockHook = uint32_t (*)();

class Registry
{
  public:
    static void  set_sink(Sink sink, void* ctx = nullptr);
    static Sink  sink() { return sink_; }
    static void* ctx() { return ctx_; }

    static void        set_clock(ClockHook hook) { clock_ = hook; }
    static void        set_color(bool on) { color_ = on; }
    static bool        color() { return color_; }
    static void        set_timestamps(bool on) { timestamps_ = on; }
    static bool        timestamps() { return timestamps_; }
    static void        set_global_level(Level l) { global_ = l; }
    static Level       global_level() { return global_; }
    static uint32_t    now_ms() { return clock_ ? clock_() : 0u; }
    static void        emit(const Record& rec);
    static const char* level_name(Level l);
    static const char* level_color(Level l);

  private:
    static Sink      sink_;
    static void*     ctx_;
    static ClockHook clock_;
    static Level     global_;
    static bool      color_;
    static bool      timestamps_;
};

class Channel
{
  public:
    constexpr explicit Channel(const char* tag, Level level = Level::Info, bool enabled = true)
        : tag_(tag), level_(level), enabled_(enabled)
    {
    }

    void error(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void warn(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void info(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void debug(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void trace(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));

    void bytes(Level level, const char* prefix, const uint8_t* data, uint16_t len) const;
    void hexdump(Level level, const char* prefix, const uint8_t* data, uint16_t len) const;

    void                enable() { enabled_ = true; }
    void                disable() { enabled_ = false; }
    void                set_level(Level l) { level_ = l; }
    [[nodiscard]] Level level() const { return level_; }
    [[nodiscard]] bool  enabled() const { return enabled_; }
    [[nodiscard]] bool  wants(Level l) const
    {
        return enabled_ && l != Level::Off &&
               static_cast<uint8_t>(l) <= static_cast<uint8_t>(GSM_LOG_LEVEL_MAX) && l <= level_ &&
               l <= Registry::global_level();
    }
    [[nodiscard]] const char* tag() const { return tag_; }

  private:
    void        vlog(Level level, const char* fmt, va_list ap) const;
    const char* tag_;
    Level       level_;
    bool        enabled_;
};

class ScopedSink
{
  public:
    explicit ScopedSink(Sink sink, void* ctx = nullptr);
    ~ScopedSink();
    ScopedSink(const ScopedSink&)            = delete;
    ScopedSink& operator=(const ScopedSink&) = delete;

  private:
    Sink  prev_sink_;
    void* prev_ctx_;
};

void stdio_sink(const Record& rec, void* ctx);
void null_sink(const Record& rec, void* ctx);

struct WriterSink
{
    using Write = void (*)(void* ctx, const uint8_t* data, uint16_t len);
    Write write;
    void* ctx;
};
void writer_sink(const Record& rec, void* ctx);

} // namespace gsm::log
