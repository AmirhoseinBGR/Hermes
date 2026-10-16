# gsm/include/gsm/core/service.hpp

## نقش فایل

قرارداد مشترک همهٔ «سرویس‌ها» (ماشین‌های حالت چندمرحله‌ای AT مثل SMS، تماس، وضعیت و USSD) و همچنین ثبت وضعیت مشترک نشست با مودم (`SessionState`). هر سرویس یک `IAtObserver` است که مراحل خود را با `pump()` جلو می‌برد و نتیجه را در قالب `AtResult` عرضه می‌کند.

## ساختارهای کلیدی

### `enum class Charset : uint8_t`
```cpp
enum class Charset : uint8_t
{
    Unknown = 0,
    Gsm,    // AT+CSCS="GSM"
    Ucs2    // AT+CSCS="UCS2"
};
```

### `struct SessionState`
وضعیت نشست جاری AT که بین سرویس‌ها مشترک است و از اجرای دستبارهٔ دستورهای تنظیمی جلوگیری می‌کند:
```cpp
struct SessionState
{
    bool    text_mode     = false;  // AT+CMGF=1 applied
    Charset charset       = Charset::Unknown;
    bool    csmp_ucs2     = false;  // CSMP parameters set for UCS2
    bool    storage_ready = false;  // AT+CPMS applied
    bool    echo_off      = false;  // ATE0 applied
    void    invalidate() { *this = SessionState{}; }
};
```
`invalidate()` (مثلاً بعد از ریست ماژول یا قطع ارتباط) همه را به حالت ناشناخته برمی‌گرداند تا سرویس‌ها دوباره پیکربندی کنند.

### `class IService : public IAtObserver`
```cpp
class IService : public IAtObserver
{
  public:
    virtual bool            active() const    = 0;  // state machine not Idle
    virtual void            pump()            = 0;  // advance one step if possible
    virtual void            cancel()          = 0;  // abort current work
    virtual const char*     step_name() const = 0;  // for logs / CLI
    virtual const AtResult& result() const    = 0;  // final result of last run
};
```
پیاده‌سازی‌های مشخص: `SmsService`، `CallService`، `StatusService`، `UssdService` — همگی `final` و با الگوی ماشین حالت گام‌به‌گام (`SmsStep`، `CallStep`، …).

## جریان کاری

```text
Modem::pump_services()
    |
    +--> if (!service.active()) start next job -> service.start_xxx()
    |
    +--> service.pump()
             |  issue AT command via AtEngine (once per step)
             v
         AtEngine -> service.on_line() / on_prompt() / on_final()
             |
             v
         advance_step() -> next state ... -> Done
```

الگوی معمول پیاده‌سازی یک سرویس:

```cpp
// A service is a step machine driven by pump(); each step issues at most
// one AT command and waits for on_final() to move on.
void SmsService::pump()
{
    if (!active() || issued_) return;
    issued_ = true;
    switch (step_) {
        case SmsStep::SetTextMode: at_.execute("AT+CMGF=1", ...); break;
        // ...
    }
}
```

## نکات فنی

- **بدون تخصیص حافظه / بدون STL**: `SessionState` یک POD کوچک است؛ سرویس‌ها همگی به‌صورت عضو `Modem` (نه اشاره‌گر پویا) نگهداری می‌شوند.
- هم‌زمان فقط **یک سرویس فعال** می‌تواند باشد؛ هماهنگی با `JobQueue` و `Modem::service_for()` انجام می‌شود.
- `active()` مبنای «آیا موتور درگیر است» برای صف و gate تماس است.
- `SessionState` عمداً بین سرویس‌ها مشترک است تا مثلاً `SetCharset` فقط یک بار اجرا شود؛ اما هر سرویسی که به پیکربندی قطعی نیاز دارد باید خودش اعتبارسنجی کند.
- `IAtObserver` پیش‌فرض‌های no-op دارد؛ سرویس‌ها فقط متدهای موردنیازشان را override می‌کنند.

## ارتباط با سایر فایل‌ها

- `core/at_engine.hpp` و `core/at_types.hpp` — موتور اجرا و اینترفیس observer
- `sms/sms_service.hpp`، `voice/call_service.hpp`، `status/status_service.hpp`، `ussd/ussd_service.hpp` — پیاده‌سازی‌های `IService`
- `modem/modem.hpp` — مالک سرویس‌ها و `SessionState`؛ صدازدن `pump()` در هر تیک
- `queue/job.hpp` — نوع Job که به هر سرویس نگاشت می‌شود
