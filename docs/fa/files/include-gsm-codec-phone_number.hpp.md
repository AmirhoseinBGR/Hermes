# gsm/include/gsm/codec/phone_number.hpp

## نقش فایل

کلاس کوچک و متمرکز برای نرمال‌سازی شماره تلفن به قالب **E.164** (مثلاً `+989123456789`). با توجه به کاربرد ایرانی کتابخانه، کد کشور پیش‌فرض `+98` است؛ ورودی‌هایی مثل `0912...` یا `912...` به شکل استاندارد بین‌المللی درمی‌آیند. این کلاس مبنای شماره‌های `SmsService` و `CallService` است.

## ساختارهای کلیدی

### `class PhoneNumber`
```cpp
class PhoneNumber
{
  public:
    PhoneNumber() = default;
    explicit PhoneNumber(const char* raw, const char* country_code = "+98")
    { assign(raw, country_code); }

    bool        assign(const char* raw, const char* country_code = "+98");
    const char* e164() const { return value_.c_str(); }
    bool        valid() const { return value_.size() >= 8; }
    void        clear() { value_.clear(); }

  private:
    StaticString<GSM_SMS_NUMBER_BYTES> value_;   // 24 bytes by default
};
```

- `assign(raw, country_code)`: نرمال‌سازی ورودی خام (با/بدون صفر اولیه، با/بدون `+`) به E.164؛ مقدار بازگشتی موفقیت عملیات را نشان می‌دهد.
- `e164()`: رشتهٔ نهایی با پیشوند `+` (برای `AT+CMGS`، `ATD` و …).
- `valid()`: حداقل ۸ رقم — فیلتر سادهٔ شماره‌های آشکارا ناقص (شمارهٔ ایرانی کامل ۱۲ رقم با کد کشور است).
- `clear()`: بازنشانی.

## جریان کاری

```text
user input: "09123456789" / "9123456789" / "+989123456789"
    |
    v
PhoneNumber::assign(raw, "+98")
    |
    v
value_ = "+989123456789"      (E.164)
    |
    +-> SmsService::start_send(number_e164, ...)
    +-> CallService::peer() / start_dial(...)
    +-> Job.number for logging & callbacks
```

نمونه:
```cpp
gsm::PhoneNumber p("09123456789");     // default country code "+98"
if (p.valid()) {
    modem.send_sms(p.e164(), "hello");
}
```

## نکات فنی

- **بدون heap / بدون STL**: ذخیره در `StaticString<GSM_SMS_NUMBER_BYTES>` (پیش‌فرض ۲۴ بایت؛ برای E.164 کافی است).
- سازنده `explicit` است تا تبدیل ضمنی رشته به شماره رخ ندهد.
- نرمال‌سازی در `assign` پیاده‌سازی شده (در .cpp)؛ هدر فقط قرارداد را نشان می‌دهد.
- `valid()` یک اعتبارسنجی حداقلی است (طول)؛ اعتبارسنجی الگوی کشور/اپراتور بر عهدهٔ کاربر است.
- شماره‌های USSD یا کدهای کوتاه ممکن است از `valid()` رد نشوند — از این کلاس برای شماره‌های مکالمه/پیامک استفاده کنید.
- `value_` همیشه null-terminated است (تضمین `StaticString`).

## ارتباط با سایر فایل‌ها

- `util/static_string.hpp` — بافر زیرساختی
- `gsm_config.hpp` — `GSM_SMS_NUMBER_BYTES`
- `sms/sms_service.hpp` — `PhoneNumber recipient_`
- `voice/call_service.hpp` — `PhoneNumber peer_`
- `sms/sms_types.hpp` — شامل‌کنندهٔ این هدر برای انواع SMS
- `queue/job.hpp` — شماره در Job (به‌صورت `StaticString`؛ نرمال‌سازی پیش از enqueue انجام می‌شود)
