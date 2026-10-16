# gsm/include/gsm/sms/sms_service.hpp

## نقش فایل

سرویس پیامک کتابخانه: ماشین حالت چندمرحله‌ای برای ارسال، خواندن، اسکن و حذف پیامک‌ها روی مودم GSM. این سرویس همهٔ جزئیات پروتکل (حالت متن، رمزگذاری UCS2/GSM-7، پارامترهای CSMP، مسیر ذخیره‌سازی، prompt ارسال و ACK نهایی) را مدیریت می‌کند و در نسخهٔ v0.4 یک **نگهبان سخت‌گیرانهٔ طول پیامک خروجی** برای جلوگیری از هنگ‌کردن ماژول‌های خانوادهٔ M08R دریافت کرده است.

## ساختارهای کلیدی

### `enum class SmsStep : uint8_t`
مراحل ماشین حالت:
```cpp
enum class SmsStep : uint8_t
{
    Idle = 0,
    SetTextMode,   // AT+CMGF=1
    SetCharset,    // AT+CSCS="UCS2"/"GSM"
    SetCsmp,       // AT+CSMP=... (from DeviceTraits)
    SetStorage,    // AT+CPMS=... (from DeviceTraits)
    SendHeader,    // AT+CMGS="..." -> wait for '>' prompt
    WritePayload,  // write UCS2-hex / GSM text
    WaitAck,       // +CMGS <mr> / final OK
    ListUnread,    // AT+CMGL="REC UNREAD"
    ReadMessage,   // AT+CMGR=<index>
    DeleteIndex,   // AT+CMGD=<index>
    DeleteRead,    // delete all read (trait-dependent)
    Done,
};
```

### `class SmsService final : public IService`

سازنده و callbackها:
```cpp
SmsService(AtEngine& engine, SessionState& session, const DeviceTraits& traits);

using SendDone = Delegate<const SmsSendReport&>;
using Received = Delegate<const SmsMessage&>;
void set_on_received(Received cb);
void set_default_encoding(SmsEncoding enc);   // default: Ucs2
void set_delete_after_read(bool on);          // default: true
```

متدهای شروع عملیات:
```cpp
bool start_send(const char* number_e164, const char* utf8_text,
                SmsEncoding enc = SmsEncoding::Auto);
bool start_read(int16_t storage_index, bool delete_after = true);
bool start_scan_unread();
bool start_delete_index(int16_t storage_index);
bool start_delete_read();
```

پیاده‌سازی `IService`:
```cpp
bool            active() const override;   // step_ != SmsStep::Idle
void            pump() override;
void            cancel() override;
const char*     step_name() const override;
const AtResult& result() const override;
void on_line(const char* line, uint16_t len) override;
void on_prompt() override;
void on_final(const AtResult& res) override;
```

دسترسی به نتایج:
```cpp
const SmsSendReport& send_report() const;
const SmsMessage&    last_message() const;
uint8_t              pending_indices() const;   // unread indices queued for reading
```

### نگهبان طول پیامک خروجی (ویژگی جدید v0.4)
```cpp
// Strict outgoing length guard (UTF-8 characters, 0 disables the guard).
// The default (GSM_SMS_MAX_CHARS) sits safely below one UCS2 SMS (70 chars)
// because the M08R family is known to hang / emit empty SMS on long payloads.
void     set_max_chars(uint16_t n);
uint16_t max_chars() const;
static uint16_t count_chars(const char* utf8);
```
- سقف بر حسب **کاراکتر UTF-8** (نه بایت)؛ مقدار پیش‌فرض `GSM_SMS_MAX_CHARS = 60` — پایین‌تر از سقف ۷۰ کاراکتری یک پیامک UCS2.
- متن بلندتر → خطا با `AtResult{status: Error, code: kErrSmsTooLong}` بدون ارسال هیچ دستوری.
- `set_max_chars(0)` نگهبان را غیرفعال می‌کند.
- دلیل: ماژول‌های خانوادهٔ M08R با payload بلند هنگ می‌کنند یا پیامک خالی می‌فرستند.

متدهای خصوصی کلیدی: `advance_step`، `fail`، `complete`، `wants_ucs2`، `parse_cmgr_header`، `store_body`، `queue_index`.

## جریان کاری

ارسال:
```text
start_send("+98912...", utf8, enc)
    |
    |  guard: count_chars(utf8) > max_chars_ ?  -> fail(kErrSmsTooLong)   [v0.4]
    v
SetTextMode -> SetCharset -> SetCsmp -> SetStorage -> SendHeader (AT+CMGS)
    |                                                |
    |                                    on_prompt() -> WritePayload + Ctrl-Z
    v                                                v
   WaitAck (+CMGS <mr> / OK)  -> Done -> send_report() + JobCallback
```

دریافت (خودکار با `+CMTI` وقتی `Modem::set_auto_read_sms(true)`):
```text
+CMTI:"SM",idx -> start_read(idx)
    -> ReadMessage (AT+CMGR) : parse_cmgr_header / store_body
    -> on_received_(SmsMessage)
    -> delete_after ? DeleteIndex : Done
```

## نکات فنی

- **بدون تخصیص حافظه / بدون STL**: بافرهای `StaticString` و آرایهٔ ایستای `pending_[GSM_SMS_INBOX_SLOTS]`.
- **نگهبان طول**: شمارش با `count_chars` واحدهای کدپوینت UTF-8 را می‌شمارد؛ برای متن فارسی (UCS2) سقف مؤثر ۷۰ است ولی پیش‌فرض ۶۰ عمداً محافظه‌کارانه است.
- رمزگذاری پیش‌فرض سرویس `Ucs2` است؛ `SmsEncoding::Auto` بر اساس `codec::is_ascii_only` انتخاب می‌شود.
- `csmp_ucs2`/`csmp_gsm` و `storage_cmd` از `DeviceTraits` می‌آیند — کد ماژول‌مستقل است.
- در `SendHeader` نشان `>` منتظر می‌مانیم (`on_prompt`)؛ ارسال زودتر از prompt باعث خرابی payload می‌شود.
- `cancel()` هر مرحله‌ای را می‌شکند و نتیجه را `Aborted` می‌کند؛ بعد از Ctrl-Z دیگر لغو ایمن نیست.
- `session_` (`SessionState`) از اجرای دوبارهٔ `SetTextMode`/`SetCharset` جلوگیری می‌کند؛ بعد از `invalidate()` همه چیز دوباره اعمال می‌شود.

## ارتباط با سایر فایل‌ها

- `core/service.hpp` — پایهٔ `IService` و `SessionState`
- `core/at_engine.hpp` — اجرای دستورهای AT
- `device/device_traits.hpp` — CSMP، storage، timeoutها، الزام شمارهٔ UCS2
- `sms/sms_types.hpp` — `SmsMessage`، `SmsSendReport`، `SmsEncoding`
- `codec/phone_number.hpp` — `PhoneNumber recipient_`
- `codec/ucs2.hpp` — تبدیل متن به hex-UCS2 برای payload
- `gsm_config.hpp` — `GSM_SMS_MAX_CHARS` و اندازه‌های بافر
- `core/at_types.hpp` — `kErrSmsTooLong`
- `modem/modem.hpp` — Jobهای `SmsSend/SmsRead/SmsScan/SmsDelete*`
