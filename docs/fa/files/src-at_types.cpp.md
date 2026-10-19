# gsm/src/at_types.cpp

## نقش فایل

پیاده سازی توابع کمکی متنی برای انواع نتیجه فرمان های AT: تبدیل `AtStatus` به رشته قابل خواندن برای لاگ، و جدول های «راهنمای خطا» برای کدهای `+CMS ERROR` و `+CME ERROR`. این فایل هیچ منطق اجرایی ندارد و صرفاً پل ارتباطی بین کدهای عددی مودم و انسان (لاگ/کنسول) است.

## توابع و منطق کلیدی

### `const char* to_string(AtStatus s)`
نگاشت یک به یک حالت های `AtStatus` به رشته های ثابت:

| `AtStatus` | خروجی |
|---|---|
| `Pending` | `"PENDING"` |
| `Ok` | `"OK"` |
| `Error` | `"ERROR"` |
| `CmeError` | `"CME_ERROR"` |
| `CmsError` | `"CMS_ERROR"` |
| `Timeout` | `"TIMEOUT"` |
| `Aborted` | `"ABORTED"` |
| `NoDialtone` | `"NO_DIALTONE"` |
| `Busy` | `"BUSY"` |
| `NoCarrier` | `"NO_CARRIER"` |
| `NoAnswer` | `"NO_ANSWER"` |

برای مقدار خارج از enum مقدار `"?"` برمی گردد (مسیر ایمنی).

### `const char* cms_error_hint(int16_t code)`
توضیح کوتاه انگلیسی برای رایج ترین خطاهای SMS (CMS):

| کد | معنا |
|---|---|
| 310 | SIM not inserted (سیم کارت نیست) |
| 311 | SIM PIN required (نیاز به PIN) |
| 313 | SIM failure (خرابی سیم کارت) |
| 321 | invalid memory index (ایندکس حافظه نامعتبر) |
| 322 | memory full (حافظه پیامک پر است) |
| 330 | SMSC address unknown (آدرس مرکز پیامک نامشخص) |
| 500 | unknown error |
| 512 | SMS send failed (network/SMSC) |
| 520 | malformed payload: CRLF sent before Ctrl+Z |
| 3518 | destination not UCS2-encoded while CSCS=UCS2 |

برای کدهای دیگر `"see module CMS error table"` برمی گردد.

### `const char* cme_error_hint(int16_t code)`
همان کار برای خطاهای عمومی تجهیز (CME):

| کد | معنا |
|---|---|
| 10 | SIM not inserted |
| 11 | SIM PIN required |
| 13 | SIM failure |
| 30 | no network service |
| 58 | invalid parameters / stuck in data-entry prompt |
| 100 | unknown |

برای کدهای دیگر `"see module CME error table"`.

## جریان کاری

این فایل state-less است؛ در `AtEngine::tick` هنگام دیدن `+CME ERROR` / `+CMS ERROR` فراخوانی می شود:

```text
AtEngine::tick ──► classify_final() ──► finish(status, code)
                          │
                          └─► kLog.warn("CMS %d: %s", code, cms_error_hint(code))
```

## نکات فنی

- **کد ۵۲۰ (CMS):** نشانه کلاسیک اشتباه در فاز payload پیامک است — ارسال CRLF قبل از Ctrl+Z باعث می شود مودم payload را خراب تفسیر کند. `SmsService` برای همین متن را بدون واسطه و فقط با `write_ctrl_z()` پایان می دهد.
- **کد 3518 (CMS):** وقتی `AT+CSCS="UCS2"` فعال است و شماره مقصد UCS2-encode نشده باشد رخ می دهد؛ `SmsService::advance_step` در مرحله `SendHeader` با `traits_.ucs2_recipient_required` این را پیشگیری می کند.
- **کد ۵۸ (CME):** معروف به گیر کردن مودم در پرامپت ورود داده؛ راه حل کتابخانه `write_escape()` در `AtEngine::abort` و timeoutهای `expect_prompt` است.
- همه رشته های خروجی ثابت (static const) هستند؛ بازگشت اشاره گر کاملاً امن و بدون تخصیص حافظه است.
- جدول ها «راهنما» هستند نه منطق کنترلی؛ هیچ کدام از کدها بر تصمیم گیری FSM تأثیر ندارند و فقط برای خوانایی لاگ به کار می روند.

## ارتباط با سایر فایل ها

- `gsm/core/at_types.hpp` — تعریف `AtStatus`، `AtResult` و ثابت های خطای کتابخانه ای (`kErrSmsTooLong = -100`، `kErrCallTimeout = -101`، `kErrJobStalled = -102`).
- `src/at_engine.cpp` — مصرف کننده اصلی این توابع در لاگ نتایج نهایی.
- `src/cli.cpp` — `to_string(AtStatus)` در نمایش نتیجه دستور `at <cmd>` و رویدادهای `[JOB#…]`.
