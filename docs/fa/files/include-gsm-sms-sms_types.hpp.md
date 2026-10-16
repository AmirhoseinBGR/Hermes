# gsm/include/gsm/sms/sms_types.hpp

## نقش فایل

انواع دادهٔ حوزهٔ پیامک: رمزگذاری متن (`SmsEncoding`)، ساختار پیام دریافتی (`SmsMessage`)، گزارش ارسال (`SmsSendReport`) و یک صندوق ورودی حلقوی ایستا (`SmsInbox`). این فایل قرارداد داده‌ای مشترک بین `SmsService`، Jobها و کاربر نهایی است.

## ساختارهای کلیدی

### `enum class SmsEncoding : uint8_t`
```cpp
enum class SmsEncoding : uint8_t
{
    Auto = 0,   // pick by content (ASCII -> GSM7, else UCS2)
    Gsm7,       // 7-bit GSM alphabet, 160 chars per SMS
    Ucs2        // UCS2, 70 chars per SMS (Persian text lands here)
};
```

### `struct SmsMessage`
```cpp
struct SmsMessage
{
    StaticString<GSM_SMS_NUMBER_BYTES> number;
    StaticString<GSM_SMS_TEXT_BYTES>   text;
    StaticString<24>                   timestamp;
    int16_t                            storage_index = -1;
    SmsEncoding                        encoding      = SmsEncoding::Auto;
    bool                               unread        = true;
    void                               clear();
};
```
- `storage_index`: ایندکس در حافظهٔ سیم‌کارت/ماژول (برای حذف بعد از خواندن).
- `timestamp`: رشتهٔ خام `+CMGL/+CMGR` (فرمت سرویس مرکز پیام).
- `clear()` همه را به حالت اولیه برمی‌گرداند.

### `struct SmsSendReport`
```cpp
struct SmsSendReport
{
    int16_t  reference = -1;   // +CMGS <mr> message reference
    AtResult result;
};
```

### `template <uint8_t N> class SmsInbox`
صندوق ورودی حلقوی بدون heap:
```cpp
SmsMessage& acquire();              // next slot (oldest overwritten), cleared
uint8_t                  count() const;
static constexpr uint8_t capacity() { return N; }
const SmsMessage&        at(uint8_t i) const;   // i=0 is the newest
void clear();
```
- الگوی ring buffer: `acquire` سر `head_` را جلو می‌برد و قدیمی‌ترین پیام بازنویسی می‌شود.
- `at(i)` از **جدیدترین** به قدیمی‌ترین ایندکس‌دهی می‌کند (`at(0)` = آخرین پیام).

## جریان کاری

```text
incoming +CMTI -> Modem (auto_read) -> SmsService::start_read(index)
    |
    v
parse +CMGR header/body -> SmsMessage last_
    |
    v
on_received_(SmsMessage)   /   SmsInbox::acquire() in application
    |
    v
optional delete_after_read -> AT+CMGD / DeleteRead step
```

ارسال:
```text
SmsService::start_send(number, utf8, enc)
    -> SmsEncoding::Auto resolved (is_ascii_only -> Gsm7 else Ucs2)
    -> on success: SmsSendReport{reference, result}
```

## نکات فنی

- **بدون تخصیص حافظه**: همهٔ رشته‌ها `StaticString`؛ `SmsInbox` آرایهٔ ایستا از `SmsMessage`.
- متن UTF-8 نگهداری می‌شود (فارسی به‌صورت UCS2 روی سیم منتقل می‌شود ولی در حافظه UTF-8 است) — تبدیل بر عهدهٔ `codec/ucs2.hpp`.
- `SmsMessage::text` سقف `GSM_SMS_TEXT_BYTES` (پیش‌فرض ۵۱۲ بایت) دارد.
- `timestamp` عمداً `StaticString<24>` ثابت است (فرمت استاندارد `yy/MM/dd,hh:mm:ss±zz`).
- `SmsInbox::at` ایندکس‌گذاری معکوس دارد؛ برای ترتیب زمانی قدیمی‌به‌جدید باید از `count()-1` به ۰ بروید.

## ارتباط با سایر فایل‌ها

- `codec/phone_number.hpp` — شمارهٔ فرستنده/گیرنده در سرویس (الگوی نرمال‌سازی E.164)
- `codec/ucs2.hpp` — تبدیل‌های رمزگذاری
- `sms/sms_service.hpp` — تولید و مصرف این انواع
- `queue/job.hpp` — فیلدهای `SmsEncoding`، `number`، `text` در Job
- `gsm_config.hpp` — `GSM_SMS_TEXT_BYTES`، `GSM_SMS_NUMBER_BYTES`
