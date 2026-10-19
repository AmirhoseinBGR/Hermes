# gsm/include/gsm/cli/cli.hpp

## نقش فایل

کنسول تعاملی صنعتی کتابخانه (`cli::Console`) — همان چیزی که قبلاً در برنامهٔ کاربردی به نام `gsm_cli` وجود داشت و اکنون به یک قابلیت رسمی **در هستهٔ کتابخانه** تبدیل شده است. هر میزبانی (STM32، Arduino، تست میزبان) کافی است بایت های ورودی را به `feed()` بدهد و خروجی را به یک `TextSink` وصل کند تا همان منوی مدیریتی (وضعیت زنده، صف، نگهبان، دستورات SMS/تماس/USSD، حالت raw AT) را داشته باشد. فرمان ها از وب/پل سریال هم با `exec_web` قابل اجراست.

## ساختارهای کلیدی

### `struct TextSink`
خروجی قابل حمل بایت محور (پورت سریال، کنسول وب، هارنس تست):
```cpp
struct TextSink
{
    void (*write)(void* ud, const char* data, uint16_t len) = nullptr;
    void* ud                                                = nullptr;

    void put(const char* s) const;
    void printf(const char* fmt, ...) const;   // __attribute__((format(printf, 2, 3)))
};
```

### `class Console final : public IAtObserver`
```cpp
Console(Modem& modem, hal::IClock& clock);

// output & integration
void set_output(const TextSink& sink);
void set_web_output(void (*out)(const char*, void*), void* ud);
void install_log_mirror();        // route gsm::log records to the console (+web)
void set_loop_rate_display(bool on);

// lifecycle & I/O
void init(bool draw_menu = true); // register callbacks, draw the menu
void tick();                      // periodic redraw / housekeeping
void feed(uint8_t byte);          // one byte of console input
void feed_line(const char* line); // one command line (no CR/LF needed)
void exec_web(const char* line);  // command from the web/serial bridge
void status_line(char* dst, unsigned cap) const; // compact live status CSV
void redraw();

// raw AT mode
bool passthrough() const;
void set_passthrough(bool on);

// IAtObserver for the raw "at" command
void on_line(const char* line, uint16_t len) override;
void on_final(const AtResult& res) override;
```

حالت های داخلی:
```cpp
enum class Mode : uint8_t { Menu, LiveStatus, Passthrough };
```

بخش های خصوصی (نمایش و فرمان):
- rendering: `out`/`outf`، `web_event`/`web_out_chunk`، `ansi_bar`/`ansi_divider`/`ansi_clear`، `kv`، `draw_menu`/`draw_live_status`/`draw_queue`/`draw_status_bar`/`draw_watchdog`/`prompt`/`redraw_current`
- command handling: `handle(char* line)`، `cycle_level()` (چرخش سطح لاگ)
- event thunks: `on_sms_thunk`، `on_call_thunk`، `on_call_ended_thunk`، `on_sim_thunk`، `on_job_thunk`، `on_health_thunk`، `log_thunk` — همگی static و متصل به `Modem` با `Delegate`

نحوهٔ استفاده (از کامنت خود هدر):
```cpp
gsm::cli::Console console(modem, clock);
console.set_output(sink);
console.init();          // registers callbacks, draws the menu
while (true) { console.tick(); ... console.feed(byte); }
```
مرجع فرمان ها: `docs/CLI.md` (فارسی) و `docs/html/cli.html`.

## جریان کاری

```text
console byte stream (UART / USB / web)
    |
    v
feed(byte) / feed_line() / exec_web()
    |
    v
handle(line) --> command dispatch (status, queue, sms, call, ussd, at, ...)
    |                        |
    |                        +--> raw "at" cmd -> AtEngine.execute(..., this)
    |                                                 on_line/on_final echo
    v
rendering: Menu / LiveStatus / Passthrough  -> TextSink (+ web mirror)
    ^
    |
Modem callbacks (on_sms / on_call / on_call_ended / on_sim / on_job / on_health)
    -> event logs & redraw          install_log_mirror() -> log records here
```

## نکات فنی

- **بدون heap**: بافر خط ورودی `char line_[128]` و بافر وب `wline_[192]` روی object؛ هیچ `std::string`/`iostream` در کار نیست.
- `Console` همزمان یک `IAtObserver` است (برای دستور raw `at`)؛ اجرای دستور با timeout ده ثانیه ای و نمایش وضعیت نهایی.
- **خروجی دوگانه**: همهٔ خروجی ها به `TextSink` اصلی و در صورت تنظیم، به آینهٔ وب (`web_write_`) هم می روند — مناسب پنل تحت وب متصل به همان مودم.
- `install_log_mirror` با `log::WriterSink` رکوردهای `gsm::log` را به کنسول می آورد؛ برای دیباگ میدانی بدون ابزار جانبی.
- `status_line` یک CSV فشرده از وضعیت می دهد (برای اسکریپت/تله متری)؛ `draw_watchdog` وضعیت `MaintenanceWatchdog` (بودجه ها، resetها، health) را نشان می دهد.
- `set_loop_rate_display` نرخ حلقهٔ اصلی را نمایش می دهد (ابزار پایش کارایی روی میکروکنترلر).
- ثبت callbackها در `init()` انجام می شود (callbackهای `Modem` را بازنویسی می کند)؛ اگر خودتان callback ثبت کرده اید، بعد از `init()` این کار را بکنید.
- `Mode::Passthrough` بایت ها را مستقیم به موتور AT می فرستد (مشابه `tools::SerialBridge`).

## ارتباط با سایر فایل ها

- `modem/modem.hpp` — شیء تحت کنترل؛ همهٔ callbackها از همین جا وصل می شوند
- `core/at_types.hpp` — `IAtObserver` برای دستور raw `at`
- `hal/hal.hpp` — `IClock` برای timestamp و نرخ حلقه
- `log/log.hpp` — `install_log_mirror` و `WriterSink`
- `maintenance/watchdog.hpp` — `HealthReport` برای `draw_watchdog` و `on_health_thunk`
- `tools/serial_bridge.hpp` — جایگزین مینیمال برای حالت passthrough
- `gsm_config.hpp` — اندازه های بافر و سطوح لاگ
