# gsm/include/gsm/core/at_types.hpp

## نقش فایل

انواع پایهٔ زبان AT کتابخانه را تعریف می کند: وضعیت نتیجهٔ یک فرمان AT، ساختار نتیجه، کدهای خطای سطح کتابخانه و اینترفیس مشاهده گر (`IAtObserver`) که همهٔ سرویس ها از طریق آن خروجی موتور AT را دریافت می کنند. این فایل قرارداد مشترک بین `AtEngine` و تمام سرویس هاست.

## ساختارهای کلیدی

### `enum class AtStatus : uint8_t`
وضعیت پایانی یک تراکنش AT:
```cpp
enum class AtStatus : uint8_t
{
    Pending = 0,
    Ok,
    Error,
    CmeError,
    CmsError,
    Timeout,
    Aborted,
    NoDialtone,
    Busy,
    NoCarrier,
    NoAnswer,
};
```
- `Ok` / `Error`: پاسخ های استاندارد `OK` / `ERROR`
- `CmeError` / `CmsError`: خطاهای `+CME ERROR: n` و `+CMS ERROR: n` (کد عددی در `code`)
- `NoDialtone` / `Busy` / `NoCarrier` / `NoAnswer`: پاسخ های پایانی مربوط به تماس
- `Timeout` / `Aborted`: اتمام زمان یا لغو دستی

### `struct AtResult`
```cpp
struct AtResult
{
    AtStatus status = AtStatus::Pending;
    int16_t  code   = -1;
    bool     ok() const { return status == AtStatus::Ok; }
};
```
`code` برای خطاهای CME/CMS کد عددی مودم و برای خطاهای سطح کتابخانه یکی از مقادیر زیر است.

### کدهای خطای سطح کتابخانه (`code < -99`)
```cpp
constexpr int16_t kErrSmsTooLong  = -100; // outgoing text exceeds the strict length guard
constexpr int16_t kErrCallTimeout = -101; // call supervision auto-hangup fired
constexpr int16_t kErrJobStalled  = -102; // job could not be started after retries
```
- **`kErrSmsTooLong` (ویژگی v0.4)**: نگهبان طول پیامک خروجی فعال شده و متن از `max_chars` بیشتر است.
- **`kErrCallTimeout` (ویژگی v0.4)**: تایمر نظارت تماس (`CallPolicy`) قطع خودکار را اجرا کرده است.
- **`kErrJobStalled`**: Job پس از چند بار تلاش (`GSM_JOB_START_RETRIES`) شروع نشده است.

### توابع تبدیل و راهنمای خطا
```cpp
const char* to_string(AtStatus s);
const char* cms_error_hint(int16_t code);
const char* cme_error_hint(int16_t code);
```
- `to_string`: نام متنی وضعیت برای لاگ/CLI
- `cms_error_hint` / `cme_error_hint`: توضیح انسان خوان برای کدهای خطای پیامک/تجهیز

### `class IAtObserver`
```cpp
class IAtObserver
{
  public:
    virtual ~IAtObserver() = default;
    virtual void on_line(const char* line, uint16_t len) { ... }
    virtual void on_prompt() {}
    virtual void on_final(const AtResult& res) { ... }
};
```
- `on_line`: هر خط پاسخ (به جز خطوط نهایی و URC)
- `on_prompt`: دریافت نشان `>` برای ورود به حالت payload (پیامک/USSD)
- `on_final`: نتیجهٔ نهایی تراکنش (OK/ERROR/Timeout/…)

همهٔ سرویس ها (`IService`) و ابزارهایی مثل `SerialBridge` و `cli::Console` این اینترفیس را پیاده می کنند.

## جریان کاری

```text
AtEngine.execute(req, observer)
        |
        |  bytes -> LineReader -> classified lines
        v
 observer->on_line(...)        // response lines (CMGL, +CLCC, ...)
 observer->on_prompt()         // '>' for SMS/USSD payload
        |
        v
 classify_final()              // OK / ERROR / +CME / +CMS / NO CARRIER ...
        |
        v
 observer->on_final(AtResult)  // transaction finished
```

## نکات فنی

- `AtResult` و `AtStatus` نوع های POD و کم حجم (`uint8_t` + `int16_t`)؛ مناسب میکروکنترلر، بدون تخصیص حافظه.
- `on_line` با `len` صدا زده می شود؛ خط لزوماً null-terminated نیست (اگرچه در عمل `LineReader` رشتهٔ null-terminated می دهد).
- کدهای `kErr*` از -۱۰۰ به پایین اند تا با کدهای مثبت CME/CMS تداخل نکنند؛ `code = -1` یعنی «بدون کد».
- `Pending` فقط برای مقداردهی اولیه است و نباید در `on_final` دیده شود.

## ارتباط با سایر فایل ها

- `core/at_engine.hpp` — تولیدکنندهٔ رویدادهای `IAtObserver` و مالک `classify_final`
- `core/service.hpp` — `IService` از `IAtObserver` ارث می برد
- `queue/job.hpp` — فیلد `AtResult result` در هر Job
- `sms/sms_service.hpp`، `ussd/ussd_service.hpp`، `voice/call_service.hpp` — مصرف کنندگان `kErrSmsTooLong` / `kErrCallTimeout`
- `tools/serial_bridge.hpp`، `cli/cli.hpp` — نمایش `to_string(res.status)`
