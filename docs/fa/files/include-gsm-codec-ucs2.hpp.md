# gsm/include/gsm/codec/ucs2.hpp

## نقش فایل

توابع تبدیل متن برای رمزگذاری UCS2 مورد نیاز پیامک‌های فارسی/عربی و تشخیص سریع ماهیت رشته‌ها. مودم‌های GSM متن UCS2 را به‌صورت **رشتهٔ hex** (هر کاراکتر ۴ رقم hex) در payload پیامک دریافت می‌کنند؛ این فایل پل بین UTF-8 داخل برنامه و آن قالب سیمی است. همهٔ توابع با بافرهای caller کار می‌کنند (بدون تخصیص حافظه).

## ساختارهای کلیدی

همه در `namespace gsm::codec`:

```cpp
uint16_t utf8_to_ucs2_hex(const char* utf8, char* out, uint16_t out_cap);
```
تبدیل UTF-8 به رشتهٔ hex-UCS2 برای ارسال (`AT+CMGS` در حالت UCS2). تعداد بایت نوشته‌شده در `out` را برمی‌گرداند؛ `out_cap` سقف بافر خروجی است. هر کاراکتر UCS2 به ۴ رقم hex تبدیل می‌شود (مثلاً «م» → `0645`).

```cpp
uint16_t ucs2_hex_to_utf8(const char* hex, char* out, uint16_t out_cap);
```
عکس عملیات قبلی: رمزگشایی رشتهٔ hex-UCS2 دریافتی (`+CMGL`/`+CMGR`/`+CUSD`) به UTF-8 برای نمایش/ذخیره.

```cpp
uint16_t ascii_to_ucs2_hex(const char* ascii, char* out, uint16_t out_cap);
```
تبدیل سریع متن خالص ASCII به hex-UCS2 (بدون مسیر کامل UTF-8) — برای شماره‌ها و رشته‌های ساده.

```cpp
bool     looks_like_ucs2_hex(const char* s);
```
تشخیص اینکه یک رشته، hex-UCS2 به نظر می‌رسد (الگوی hex با طول زوج) — برای انتخاب خودکار مسیر رمزگشایی.

```cpp
uint16_t ucs2_units(const char* utf8);
```
تعداد واحدهای UCS2 لازم برای متن UTF-8 — برای محاسبهٔ طول مؤثر پیامک (سقف ۷۰ واحد در هر پیامک UCS2) و هم‌راستا با نگهبان طول `SmsService`.

```cpp
bool is_ascii_only(const char* utf8);
```
آیا متن فقط ASCII است؟ (تعیین `SmsEncoding::Auto`: متن خالص ASCII می‌تواند GSM-7 شود — ۱۶۰ کاراکتر به‌ازای هر پیامک).

## جریان کاری

ارسال پیامک فارسی:
```text
utf8 text ("سلام")
    |
    v
is_ascii_only? --yes--> GSM-7 path (160 chars/SMS)
    | no
    v
utf8_to_ucs2_hex() -> "0633064406270645"
    |
    v
AT+CMGS -> '>' prompt -> write hex payload + Ctrl-Z
```

دریافت:
```text
+CMGR body: "0633064406270645"
    |
    v
looks_like_ucs2_hex? -> ucs2_hex_to_utf8() -> SmsMessage.text (UTF-8)
```

## نکات فنی

- **بدون تخصیص حافظه**: همهٔ توابع روی بافرهای `out` کاربر می‌نویسند و طول خروجی را برمی‌گردانند؛ هیچ حالت پنهان یا سراسری وجود ندارد (thread-safe / reentrant).
- `out_cap` باید حداقل ۴× طول متن (برای hex خروجی) یا ۴× تعداد hex digits تقسیم بر ۳ (برای UTF-8 خروجی) باشد؛ فراخواننده مسئول تأمین فضا است.
- نکتهٔ UTF-8: کاراکترهای فارسی در UTF-8 دو بایتی‌اند و به یک واحد UCS2 نگاشت می‌شوند؛ `ucs2_units` همین را می‌شمارد (کاراکترهای BMP). نمادهای خارج از BMP (که به surrogate pair نیاز دارند) ممکن است به‌درستی پشتیبانی نشوند.
- `looks_like_ucs2_hex` یک تشخیص اکتشافی است (heuristic)؛ قطعیت با DCS فیلد `+CMGL/+CMGR` به دست می‌آید.
- این توابع در .cpp پیاده‌سازی شده‌اند؛ هدر فقط قرارداد دارد — مناسب جداسازی هستهٔ codec از منطق سرویس.
- طول مؤثر پیامک: ۷۰ واحد UCS2 (یا ۱۶۰ کاراکتر GSM-7)؛ به‌همین دلیل نگهبان `GSM_SMS_MAX_CHARS=60` در `SmsService` زیر این سقف است.

## ارتباط با سایر فایل‌ها

- `sms/sms_service.hpp` — تبدیل payload ارسالی/دریافتی و تصمیم `wants_ucs2()`
- `sms/sms_types.hpp` — `SmsEncoding`
- `ussd/ussd_service.hpp` — رمزگشایی پاسخ‌های `+CUSD` با DCS متناظر
- `device/device_traits.hpp` — `ucs2_recipient_required`، `csmp_ucs2`
- `util/static_string.hpp` — کمک‌های متنی مکمل
