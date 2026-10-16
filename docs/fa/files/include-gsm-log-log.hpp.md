# gsm/include/gsm/log/log.hpp

## نقش فایل

سامانهٔ ثبت وقایع (لاگ) سبک‌وزن و بدون تخصیص حافظه کتابخانه: سطح‌بندی، کانال‌های تگ‌دار، خروجی قابل تعویض (sink)، رنگ ANSI و نشانگر زمان. این سامانه عمداً جایگزین `printf` خام و `std::ostream` شده تا روی میکروکنترلرها هم قابل استفاده باشد و در عین حال در تست‌های میزبانی به‌راحتی قابل capture باشد.

## ساختارهای کلیدی

### `enum class Level : uint8_t`
```cpp
enum class Level : uint8_t { Off = 0, Error, Warn, Info, Debug, Trace };
```
سقف کامپایل‌تایم با `GSM_LOG_LEVEL_MAX` (پیش‌فرض ۴ = Debug).

### `struct Record`
```cpp
struct Record
{
    Level       level;
    const char* tag;
    const char* text;
    uint16_t    len;
    uint32_t    timestamp_ms;
};
```
واحد انتقال به sink؛ `text` بافر داخلی با اندازهٔ `GSM_LOG_BUFFER_SIZE` است.

### `class Registry` (پیکربندی سراسری، همه static)
```cpp
static void  set_sink(Sink sink, void* ctx = nullptr);
static void  set_clock(ClockHook hook);     // for timestamps
static void  set_color(bool on);
static void  set_timestamps(bool on);
static void  set_global_level(Level l);
static void  emit(const Record& rec);
static const char* level_name(Level l);
static const char* level_color(Level l);
```
typedefها:
```cpp
using Sink      = void (*)(const Record& rec, void* ctx);
using ClockHook = uint32_t (*)();
```

### `class Channel`
کانال تگ‌دار برای هر جزء کتابخانه (مثلاً `"sms"`, `"call"`):
```cpp
constexpr explicit Channel(const char* tag, Level level = Level::Info, bool enabled = true);

void error(const char* fmt, ...) const;  // __attribute__((format(printf, 2, 3)))
void warn(const char* fmt, ...)  const;
void info(const char* fmt, ...)  const;
void debug(const char* fmt, ...) const;
void trace(const char* fmt, ...) const;

void bytes(Level level, const char* prefix, const uint8_t* data, uint16_t len) const;
void hexdump(Level level, const char* prefix, const uint8_t* data, uint16_t len) const;

bool wants(Level l) const;  // enabled && l <= GSM_LOG_LEVEL_MAX && l <= channel & global level
```
`bytes`/`hexdump` برای دامپ ترافیک AT مفیدند.

### `class ScopedSink`
```cpp
explicit ScopedSink(Sink sink, void* ctx = nullptr);
~ScopedSink();   // restores previous sink + ctx
```
تعویض موقت sink (مثلاً در تست‌ها) با تضمین بازگشت در destructor — غیرقابل کپی.

### sinkهای آماده
```cpp
void stdio_sink(const Record& rec, void* ctx);   // printf-based
void null_sink(const Record& rec, void* ctx);    // discard

struct WriterSink
{
    using Write = void (*)(void* ctx, const uint8_t* data, uint16_t len);
    Write write;
    void* ctx;
};
void writer_sink(const Record& rec, void* ctx);  // bridge to any byte writer
```

## جریان کاری

```text
channel.info("registered on %s", op)
    |
    +-> channel.wants(Level::Info)?   (three-way level gate)
    |         no -> drop (zero-cost in practice)
    v
vlog() -> format into internal buffer -> build Record -> Registry::emit()
    |
    v
Sink (stdio_sink / writer_sink / user sink / null_sink)
    |                    \
    v                     v
console/UART          CLI web mirror (cli::Console::install_log_mirror)
```

نمونه:
```cpp
// Board wires a sink once at startup.
gsm::log::Registry::set_sink(gsm::log::stdio_sink, nullptr);
gsm::log::Registry::set_clock([]() { return millis(); });

// Modules use tagged channels.
static gsm::log::Channel log_sms{"sms", gsm::log::Level::Debug};
log_sms.info("send ok ref=%d", ref);
```

## نکات فنی

- **بدون heap / بدون STL**: فقط `<cstdio>`/`<cstdarg>`؛ بافر قالب‌بندی داخلی و ایستا.
- گیت سه‌سطحه در `wants()` — سطح کانال، سطح سراسری و `GSM_LOG_LEVEL_MAX` کامپایل‌تایم؛ پیام‌های بالاتر از سقف حتی قالب‌بندی هم نمی‌شوند.
- `__attribute__((format(printf, 2, 3)))` برای بررسی قالب رشته در زمان کامپایل.
- `Registry` همه‌static است؛ در چند context (مثلاً چند instance از Modem) یک sink سراسری مشترک دارید — با `ScopedSink` در تست‌ها ایزوله کنید.
- رنگ ANSI با `set_color`/`GSM_LOG_COLOR_DEFAULT` کنترل می‌شود؛ برای خروجی‌های غیرترمینالی غیرفعالش کنید.
- متن رکورد فقط تا `GSM_LOG_BUFFER_SIZE` (پیش‌فرض ۱۸۰) بایت؛ پیام‌های بلند بریده می‌شوند.

## ارتباط با سایر فایل‌ها

- `gsm_config.hpp` — `GSM_LOG_LEVEL_MAX`، `GSM_LOG_BUFFER_SIZE`، `GSM_LOG_COLOR_DEFAULT`
- `cli/cli.hpp` — `install_log_mirror()` و `WriterSink` برای انتقال لاگ به کنسول/وب
- `tools/serial_bridge.hpp` — نمایش ترافیک در حالت bridge
- همهٔ سرویس‌ها و `AtEngine` — کانال‌های تگ‌دار برای ردیابی جریان AT
