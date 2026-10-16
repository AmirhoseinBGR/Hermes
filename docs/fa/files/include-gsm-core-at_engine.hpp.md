# gsm/include/gsm/core/at_engine.hpp

## نقش فایل

موتور اجرای فرمان‌های AT: ارسال دستور روی UART، جمع‌آوری خطوط پاسخ با `LineReader`، تشخیص خطوط نهایی (OK/ERROR/…)، هدایت URCها به `UrcRouter` و اطلاع‌رسانی به `IAtObserver`. این کلاس قلب همگام‌سازی کتابخانه است و همهٔ سرویس‌ها (SMS، تماس، وضعیت، USSD) از طریق آن با مودم حرف می‌زنند.

## ساختارهای کلیدی

### `struct AtRequest`
```cpp
struct AtRequest
{
    const char* command       = nullptr;
    uint32_t    timeout_ms    = GSM_AT_DEFAULT_TIMEOUT;
    bool        expect_prompt = false; // wait for '>' (SMS/USSD payload mode)
    bool        echo_expected = true;  // module echoes the command; skip that line
};
```

### `class AtEngine`

سازنده:
```cpp
AtEngine(hal::IUart& uart, hal::IClock& clock, UrcRouter& urc);
```

متدهای اصلی:
```cpp
bool idle() const;   // no transaction in flight (observer_ == nullptr)
bool busy() const;

bool execute(const AtRequest& req, IAtObserver* observer);
bool execute(const char* cmd, uint32_t timeout_ms, IAtObserver* observer);

uint16_t write_raw(const uint8_t* data, uint16_t len);
uint16_t write_raw(const char* text);
void     write_ctrl_z();  // 0x1A — terminate SMS/USSD payload
void     write_escape();  // 0x1B — abort prompt mode

bool await_final(uint32_t timeout_ms, IAtObserver* observer);

void abort();   // cancel current transaction -> on_final(Aborted)
void tick();    // poll UART, feed LineReader, classify lines, enforce timeout

void        set_trace(bool on);
uint32_t    now();
hal::IUart& uart();
```

- `execute`: شروع تراکنش؛ دستور را با `\r` می‌فرستد، observer را ثبت می‌کند و `false` برمی‌گرداند اگر موتور مشغول باشد.
- `write_raw` / `write_ctrl_z` / `write_escape`: ارسال payload پس از نشان `>` (متن پیامک، پاسخ USSD).
- `await_final`: انتظار برای نتیجهٔ نهایی با observer داده‌شده (برای فازهایی که observer عوض می‌شود).
- `tick`: باید در هر تیک حلقهٔ اصلی صدا زده شود؛ دریافت UART، تغذیهٔ `LineReader`، تشخیص prompt (`>`)، طبقه‌بندی خطوط نهایی با `classify_final`، اعمال timeout و در نهایت `finish(status, code)`.

متدهای خصوصی:
```cpp
void finish(AtStatus status, int16_t code);   // -> observer->on_final(...)
bool classify_final(const char* line, AtStatus& status, int16_t& code) const;
```

فیلدهای وضعیت: `reader_` (خط جاری)، `observer_` (تراکنش جاری)، `sent_` (`StaticString<GSM_CMD_MAX>` دستور ارسال‌شده برای تشخیص echo)، `started_`/`timeout_`/`last_done_` (تایمرها)، `expect_prompt_` و `trace_`.

## جریان کاری

```text
service.pump()
    |  AtEngine.execute(cmd, timeout, this)
    v
loop: AtEngine.tick()
    |  uart.read() -> LineReader.feed(byte)
    |      byte=='>'  -> observer->on_prompt()     (if expect_prompt)
    |      full line  -> UrcRouter.dispatch()      (URC goes to Modem, not observer)
    |                   else -> observer->on_line()
    |                   else -> classify_final() -> finish() -> observer->on_final()
    |  timeout elapsed -> finish(Timeout)
    v
service.on_final() -> advance state machine
```

## نکات فنی

- **غیرمسدودکننده و بدون تخصیص حافظه**: هیچ `new`/`malloc`؛ همهٔ بافرها `StaticString` با اندازهٔ کامپایل‌تایم.
- یک تراکنش در هر لحظه (observer منحصربه‌فرد)؛ `execute` در حالت busy برمی‌گردد `false` — مدیریت صف بر عهدهٔ `Modem`/`JobQueue` است.
- URCها (مثلاً `RING`، `+CMTI`) در میانهٔ پاسخ هم ممکن است برسند؛ `tick` آن‌ها را از خطوط پاسخ تفکیک و به `UrcRouter` می‌فرستد.
- `echo_expected` برای ماژول‌هایی است که دستور را echo می‌کنند؛ خط echo به observer داده نمی‌شود.
- `trace_` (پیش‌فرض روشن) ترافیک AT را برای `log`/CLI ثبت می‌کند.
- wrap-around ساعت `uint32` با الگوی `hal::elapsed` پوشش داده شده است.
- گارد زمانی بین تراکنش‌ها از `GSM_AT_GUARD_TIME` می‌آید (`last_done_`)؛ timeoutهای پیش‌فرض از `gsm_config.hpp`.

## ارتباط با سایر فایل‌ها

- `core/at_types.hpp` — `AtStatus`، `AtResult`، `IAtObserver`
- `core/line_reader.hpp` — تفکیک بایت‌ها به خطوط و prompt
- `core/urc_router.hpp` — مسیریابی خطوط URC
- `hal/hal.hpp` — `IUart`، `IClock`
- `core/service.hpp` — `IService` (observer سرویس‌ها)
- `modem/modem.hpp` — مالک `AtEngine` و هماهنگ‌کنندهٔ صف/سرویس‌ها
- `tools/serial_bridge.hpp`، `cli/cli.hpp` — اجرای مستقیم دستور AT روی همین موتور
