# gsm/include/gsm/gsm_config.hpp

## نقش فایل

این فایل قلب پیکربندی کتابخانهٔ GSM است: همهٔ ثابت‌های زمان‌بندی، اندازهٔ بافرها، سقف‌ها و آستانه‌های بازیابی خطا در همین‌جا با ماکروهای `#ifndef` تعریف شده‌اند تا کاربر نهایی بتواند بدون دست‌زدن به کد کتابخانه، هر مقدار را قبل از include تغییر دهد یا از طریق `GSM_USER_CONFIG` یک فایل تنظیمات سفارشی تزریق کند. هیچ کد اجرایی در این فایل وجود ندارد؛ صرفاً قراردادهای عددی کتابخانه.

## ساختارهای کلیدی

هیچ class/struct ندارد؛ فقط ماکروها. گروه‌های اصلی:

### ورودی سفارشی کاربر
```cpp
#if defined(GSM_USER_CONFIG)
#include GSM_USER_CONFIG
#endif
```
اگر `GSM_USER_CONFIG` به‌صورت نام فایل تعریف شود، آن فایل اول از همه include می‌شود؛ بنابراین هر `#define` در فایل کاربر بر ماکروهای پیش‌فرض این فایل مقدم است.

### بافرها
| ماکرو | مقدار پیش‌فرض | کاربرد |
|---|---|---|
| `GSM_RX_RING_SIZE` | 1024 | اندازهٔ حلقهٔ دریافت UART |
| `GSM_LINE_MAX` | 512 | حداکثر طول یک خط AT |
| `GSM_CMD_MAX` | 256 | حداکثر طول دستور ارسالی |
| `GSM_SMS_TEXT_BYTES` | 512 | بافر متن پیامک |
| `GSM_SMS_NUMBER_BYTES` | 24 | بافر شماره تلفن |
| `GSM_SMS_INBOX_SLOTS` | 4 | تعداد اسلات‌های صندوق ورودی |
| `GSM_USSD_RESPONSE_BYTES` | 512 | بافر پاسخ USSD |

### نگهبان سخت‌گیرانهٔ طول پیامک (ویژگی v0.4)
```cpp
// Strict outgoing length guard (in UTF-8 characters). A single UCS2 SMS carries
// 70 chars; the default keeps a hard safety margin below that. Runtime adjustable
// via Modem::sms_policy().max_chars (0 disables the guard).
#ifndef GSM_SMS_MAX_CHARS
#define GSM_SMS_MAX_CHARS 60
#endif
```
سقف خروجی بر حسب **کاراکتر UTF-8**؛ چون یک پیامک UCS2 حداکثر ۷۰ کاراکتر گنجایش دارد، مقدار پیش‌فرض ۶۰ حاشیهٔ ایمنی ایجاد می‌کند. در زمان اجرا با `SmsService::set_max_chars()` قابل تغییر است (مقدار ۰ نگهبان را غیرفعال می‌کند).

### صف وظایف
| ماکرو | مقدار | کاربرد |
|---|---|---|
| `GSM_QUEUE_DEPTH` | 6 | ظرفیت صف Job |
| `GSM_JOB_MAX_AGE_MS` | 45000 | حذف Job کهنه در صف (۴۵ ثانیه) |
| `GSM_JOB_START_RETRIES` | 3 | دفعات تلاش برای شروع Job پیش از شکست |

### زمان‌بندی (میلی‌ثانیه)
`GSM_AT_DEFAULT_TIMEOUT` (1000)، `GSM_AT_PROMPT_TIMEOUT` (5000)، `GSM_AT_CMGS_TIMEOUT` (10000)، `GSM_AT_USSD_TIMEOUT` (10000)، `GSM_AT_GUARD_TIME` (30)، `GSM_REGISTRATION_TIMEOUT` (45000)، `GSM_PWRKEY_PULSE_MS` (1200: حداقل پهنای پولس PWRKEY طبق دیتاشیت SIM800/M08)، `GSM_MODEM_BOOT_GUARD_MS` (2500: سکوت لازم بعد از رهاکردن PWRKEY پیش از پروب `AT`).

### نظارت بر تماس (ویژگی v0.4)
```cpp
#ifndef GSM_CALL_ANSWER_TIMEOUT_MS
#define GSM_CALL_ANSWER_TIMEOUT_MS 20000 // MO call: auto-hangup if not established
#endif
#ifndef GSM_CALL_RING_TIMEOUT_MS
#define GSM_CALL_RING_TIMEOUT_MS 20000 // MT call: auto-reject if not answered
#endif
#ifndef GSM_CALL_CLCC_POLL_MS
#define GSM_CALL_CLCC_POLL_MS 2000 // +CLCC poll interval while a call is non-idle
#endif
#ifndef GSM_CALL_CLCC_MISSES
#define GSM_CALL_CLCC_MISSES 2 // empty +CLCC results before the call is declared dead
#endif
```
این چهار مقدار مقادیر پیش‌فرض `CallPolicy` را می‌سازند.

### نگهبان صنعتی و بازیابی خطا
- `GSM_CONSECUTIVE_TIMEOUTS_RESET` (3) — تعداد timeout پیاپی پیش از ورود به بازیابی
- `GSM_SIM_RETRY_INTERVAL_MS` (10000) — فاصلهٔ پروب مجدد SIM
- `GSM_CFUN_CYCLE_INTERVAL_MS` (25000) — فاصلهٔ چرخهٔ CFUN=0/1
- `GSM_HARDWARE_REBOOT_TIMEOUT_MS` (90000) — پس از ۹۰ ثانیه گیرکردن، power cycle کامل

### بازنشانی‌های تعمیراتی / نگهبان دوگانه (ویژگی v0.4)
```cpp
#ifndef GSM_PERIODIC_RESET_MS
#define GSM_PERIODIC_RESET_MS 3600000 // resetter #1: full module reset every 60 min (0=off)
#endif
#ifndef GSM_OPS_RESET_COUNT
#define GSM_OPS_RESET_COUNT 50 // resetter #2: full reset after N operations (0=off)
#endif
#ifndef GSM_WATCHDOG_GRACE_MS
#define GSM_WATCHDOG_GRACE_MS 10000 // force the reset if no safe window appears within this
#endif
```

### ثبت وقایع
- `GSM_LOG_LEVEL_MAX` (4) — سطح ماکزیمم کامپایل‌تایم: 0=off … 5=trace
- `GSM_LOG_BUFFER_SIZE` (180) — اندازهٔ بافر رکورد لاگ
- `GSM_LOG_COLOR_DEFAULT` (1) — رنگ ANSI به‌صورت پیش‌فرض روشن

## جریان کاری

این فایل در ابتدای زنجیرهٔ include قرار می‌گیرد و همه‌جا مصرف می‌شود:

```text
GSM_USER_CONFIG (optional)
        |
        v
 gsm_config.hpp  ----->  LineReader / StaticString  (buffer sizes)
        |            \-->  AtEngine                  (timeouts)
        |            \-->  JobQueue / Job            (queue depth, retries)
        |            \-->  SmsService                (max_chars guard)
        |            \-->  CallService/CallPolicy    (call supervision)
        |            \-->  MaintenanceWatchdog       (dual resetters)
        \-------------->  log::Channel               (log level ceiling)
```

## نکات فنی

- **زمان اجرا در برابر کامپایل**: همهٔ ماکروها فقط مقادیر پیش‌فرض هستند؛ بیشترشان در زمان اجرا قابل بازنویسی‌اند (مثلاً `CallPolicy`، `ResetPolicy`، `HousekeepingConfig` و `set_max_chars`). مقدار ۰ در `GSM_PERIODIC_RESET_MS` و `GSM_OPS_RESET_COUNT` یعنی «غیرفعال».
- **بدون STL / بدون تخصیص حافظه**: فایل صرفاً ماکرو است و هیچ وابستگی به کتابخانهٔ استاندارد ندارد.
- `GSM_SMS_MAX_CHARS` بر حسب کاراکتر UTF-8 است نه بایت؛ شمارش با `SmsService::count_chars()` انجام می‌شود.
- تغییر `GSM_LINE_MAX` یا `GSM_CMD_MAX` مستقیماً حافظهٔ ایستای `StaticString`ها را تغییر می‌دهد؛ در میکروکنترلرهای کم‌حافظه با احتیاط کم کنید.
- ماکروهای نگهبان صنعتی (`GSM_CONSECUTIVE_TIMEOUTS_RESET` و …) با `MaintenanceWatchdog` (بازنشانی‌های دوره‌ای) مکمل‌اند و هم‌پوشانی ندارند: اولی واکنش به خطا، دومی پیشگیری برنامه‌ریزی‌شده.

## ارتباط با سایر فایل‌ها

- `core/line_reader.hpp` — از `GSM_LINE_MAX` استفاده می‌کند
- `core/at_engine.hpp` — timeoutهای AT و `GSM_CMD_MAX`
- `queue/job.hpp` و `queue/job_queue.hpp` — `GSM_QUEUE_DEPTH`، `GSM_JOB_*`
- `sms/sms_service.hpp` — `GSM_SMS_MAX_CHARS` (نگهبان طول)
- `voice/call_service.hpp` — `GSM_CALL_*` (پیش‌فرض‌های `CallPolicy`)
- `maintenance/watchdog.hpp` — `GSM_PERIODIC_RESET_MS`، `GSM_OPS_RESET_COUNT`، `GSM_WATCHDOG_GRACE_MS`
- `log/log.hpp` — `GSM_LOG_LEVEL_MAX`، `GSM_LOG_BUFFER_SIZE`
- `device/device_traits.hpp` — `GSM_AT_PROMPT_TIMEOUT` و `GSM_AT_CMGS_TIMEOUT` در traitها
