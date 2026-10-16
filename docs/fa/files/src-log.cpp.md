# gsm/src/log.cpp

## نقش فایل

پیاده‌سازی زیرسیستم لاگ کتابخانه در فضای نام `gsm::log`: ثبت سراسری (`Registry`) با سینک قابل‌تعویض، کانال‌های چندسطحی (`Channel`) با فرمت‌دهی printf-مانند، دامپ بایتی/هگز برای ترافیک AT، و سینک‌های آماده (`stdio`، `null`، `writer` برای ارسال به سریال/وب). همه پیام‌ها با بافر ثابت ساخته می‌شوند؛ هیچ تخصیص حافظه پویا در مسیر لاگ وجود ندارد.

## توابع و منطق کلیدی

### ایستاهای `Registry` و مدیریت سینک
```cpp
Sink      Registry::sink_       = &stdio_sink;   // پیش‌فرض: printf
ClockHook Registry::clock_      = nullptr;
Level     Registry::global_     = Level::Trace;
bool      Registry::color_      = (GSM_LOG_COLOR_DEFAULT != 0);
bool      Registry::timestamps_ = true;
```

- `set_sink(Sink, void* ctx)` — تعویض سینک؛ اگر `nullptr` بدهید به `stdio_sink` بازمی‌گردد (fail-safe).
- `emit(const Record&)` — ارسال یک رکورد به سینک جاری.
- `level_name(Level)` → `"E" | "W" | "I" | "D" | "T"`؛ `level_color(Level)` → کدهای ANSI متناظر (قرمز/زرد/فیروزه‌ای/سفید/کم‌رنگ).

### `void Channel::vlog(Level, const char* fmt, va_list) const`
هسته فرمت‌دهی: متن با `vsnprintf` در بافر ثابت `GSM_LOG_BUFFER_SIZE` (پیش‌فرض ۱۸۰ بایت) ساخته، طول واقعی (برش‌خورده) در `Record` ثبت و با timestamp از `Registry::now_ms()` ارسال می‌شود. فیلتر سطح (`wants(level)`) **قبل از** فرمت‌دهی انجام می‌شود تا پیام‌های ناخواسته هزینه‌ای نداشته باشند.

### متدهای سطح (تولیدشده با ماکرو `GSM_LOG_IMPL`)
`error`، `warn`، `info`، `debug`، `trace` — همگی الگوی یکسان دارند: بررسی سطح، بازکردن `va_list`، فراخوانی `vlog`. ماکرو بعد از تولید `#undef` می‌شود.

### `void Channel::bytes(Level, const char* prefix, const uint8_t* data, uint16_t len) const`
نمایش ترافیک خام به‌صورت خوانا: چاپی‌های ASCII مستقیم، `\r` → `\r` متنی (`\\r`)، `\n` → `\\n`، `0x1A` → `<CtrlZ>`، `0x1B` → `<ESC>` و بقیه به `<XX>`. برای ردیابی payload پیامک و پاسخ‌های AT حیاتی است.

### `void Channel::hexdump(Level, const char* prefix, const uint8_t* data, uint16_t len) const`
دامپ هگز ۱۶‌بایتی در هر خط با آفست: `"%s%04X: %02X %02X …"`؛ هر خط یک `Record` جداگانه است.

### `ScopedSink`
RAII برای تعویض موقت سینک (مثلاً منحرف‌کردن لاگ به بافر تست)؛ در destructor سینک قبلی بازمی‌گردد.

### سینک‌های آماده
- **`stdio_sink`** — خروجی `printf` با قالب `[%8lu][%s/%s] %s` (زمان اختیاری) و رنگ ANSI اختیاری.
- **`null_sink`** — بی‌صدا؛ برای حذف کامل لاگ در محصول نهایی.
- **`writer_sink`** — همان قالب را در یک بافر می‌سازد و به `WriterSink::write` (تابع ارجاعی کاربر، مثلاً پورت سریال `Console`) می‌دهد؛ این همان مسیری است که `cli::Console::install_log_mirror` استفاده می‌کند.

## جریان کاری

```text
kLog.info("sent, ref=%d", ref)                 // هر فایل: static const Channel
   │  wants(Info)? ──► خیر: پایان (بدون هزینه)
   ▼
vlog() ──► vsnprintf به بافر 180 بایتی ──► Record{level, tag, text, len, ts}
   ▼
Registry::emit ──► sink_(rec, ctx)
                    ├─ stdio_sink   → printf مستقیم
                    ├─ writer_sink  → WriterSink::write (سریال/وب Console)
                    └─ null_sink    → حذف
```

## نکات فنی

- **فیلتر زودهنگام:** `wants(level)` قبل از هر کاری اجرا می‌شود؛ لاگ در سطح بالاتر (مثلاً Error در production) تقریباً رایگان است.
- **برش بافر:** پیام‌های بلندتر از `GSM_LOG_BUFFER_SIZE` بی‌صدا بریده می‌شوند (`vsnprintf` مقدار برگشتی بالاتر از ظرفیت را نادیده می‌گیرد).
- **`Channel` ثابت‌های محلی هر فایل:** هر فایل `.cpp` یک `static const log::Channel kLog{"tag", Level::Debug}` دارد؛ tag در خروجی `[tag]` دیده می‌شود و سطح پیش‌فرض هر زیرسیستم مستقل است.
- **timestamp از `ClockHook`:** `Registry::set_clock` تابع بازگشتی `uint32_t` می‌گیرد؛ `Modem::begin` آن را به `millis()` سخت‌افزار وصل می‌کند (قبل از آن، زمان‌ها 0 است).
- **`bytes` برای `AtEngine::write_raw` در حالت trace** استفاده می‌شود (`TX< `)؛ `hexdump` بیشتر برای دیباگ لایه انتقال.
- **قالب یکسان در همه سینک‌ها:** `writer_sink` و `stdio_sink` هر دو `[%time][LEVEL/tag] text` می‌سازند؛ تفاوت فقط مقصد است.
- **بدون heap و thread-safe نبودن:** `Registry` ایستاهای سراسری ساده دارد (برای سیستم‌های single-threaded مانند حلقه اصلی `Modem::tick` طراحی شده).

## ارتباط با سایر فایل‌ها

- `gsm/log/log.hpp` — تعریف `Level`، `Record`، `Channel`، `Registry`، `Sink`، `WriterSink`، `ScopedSink`.
- `include/gsm/gsm_config.hpp` — `GSM_LOG_BUFFER_SIZE`، `GSM_LOG_LEVEL_MAX`، `GSM_LOG_COLOR_DEFAULT`.
- مصرف‌کننده‌ها: تقریباً همه فایل‌های `src/` (هرکدام `kLog` محلی دارند).
- `src/cli.cpp` — نصب سینک `writer_sink` برای آینه‌سازی لاگ در کنسول/وب، و کنترل سطح/رنگ/زمان با دستورهای `L`/`T`/`C`.
- `src/modem.cpp` — وصل کردن `ClockHook` به `millis()` در `begin()`.
