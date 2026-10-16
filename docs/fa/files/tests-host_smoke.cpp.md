# gsm/tests/host_smoke.cpp

## نقش فایل

تست دود (smoke test) میزبان برای کتابخانه‌ی GSM. این تست بدون سخت‌افزار واقعی و با استفاده از پُرت میزبان (`HostUart`/`HostClock`/`HostPowerControl`) تمام رفتارهای بحرانی هسته را به‌صورت قطعی (deterministic) راستی‌آزمایی می‌کند: کدک‌های UCS2، تشخیص اپراتور، ترتیب بوت، کد USSD تعادل، نگهبان سخت‌گیرانه‌ی طول پیامک، نظارت تماس با قطع خودکار، تشخیص URC پایان تماس از‌دست‌رفته، دروازه‌ی صف در برابر تماس، بودجه‌های دوگانه‌ی بازنشانی نگهداری، پروب‌های سلامت، بازیابی SIM، پاک‌سازی صف و واچ‌داگ سخت‌افزاری.

## ساختارها/توابع کلیدی

نمونه‌های سراسری:

- `static port::HostUart<2048> uart` — UART شبیه‌سازی‌شده (تزریق/بازرسی).
- `static port::HostClock clock_` — زمان‌سنج مصنوعی (`now`).
- `static port::HostPowerControl pwr` — شمارنده‌ی پالس تغذیه.
- `static Modem modem(uart, clock_, kSim800, &pwr)` — مودم SIM800 زیر تست.

توابع کمکی:

- `static void advance(uint32_t ms, uint8_t rounds = 4)`
  زمان `clock_.now` را `ms` جلو می‌برد و `modem.tick()` را `rounds` بار صدا می‌زند (پیشرفت زمان کنترل‌شده).

- `static bool tx_has(const char* needle)`
  بررسی می‌کند آیا `needle` در بافر ارسالی (`uart.tx()`) وجود دارد (یعنی مودم چنین دستوری صادر کرده).

- `int main()`
  تمام سناریوها را به‌ترتیب اجرا و با `assert` راستی‌آزمایی می‌کند.

## جریان کاری

تست‌ها در `main` به‌ترتیب:

1. **کدک‌ها و تشخیص اپراتور:** `codec::utf8_to_ucs2_hex("سلام")` → `"0633064406270645"`؛ `detect_operator` برای Irancell/MCI/RighTel؛ کدهای پیش‌فرض تعادل USSD (`*141*1#`, `*140*11#`, `*140#`).
2. **ترتیب بوت:** `modem.begin()` → پاسخ `AT` → `OK` → `WaitingRegistration` → تزریق `+CREG: 0,1` → پاسخ به فرمان‌های بوت‌استرپ تا `modem.ready()`.
3. **پرس‌وجوی USSD:** `modem.check_balance()` → `AT+CUSD=` → تزریق `+CUSD` → اتمام شغل (`JobKind::None`).
4. **نگهبان سخت‌گیرانه‌ی طول پیامک:** متن خیلی بلند → `kErrSmsTooLong` و `kInvalidJob` بدون `AT+CMGS`؛ محدودیت با `set_max_chars` قابل تنظیم؛ متن کوتاه قبول می‌شود.
5. **نظارت تماس (قطع خودکار پس از ۲۰ ثانیه):** `modem.dial` → `ATD...;` → `CallState::Outgoing` → حلقه‌ی زمان → `AT+CHUP`/`ATH` → `CallState::Idle` با `CallEndReason::Timeout`.
6. **تشخیص URC پایان تماس از‌دست‌رفته + دروازه‌ی صف:** تماس دوم → پیامک در حین تماس نگه داشته می‌شود (بدون `AT+CMGS`) → لیست `+CLCC` خالی (مثل M08R که `NO CARRIER` نمی‌فرستد) → پایان تماس با `CallEndReason::NoAnswer` → صف دوباره حرکت می‌کند.
7. **بازنشانی‌های دوگانه‌ی نگهداری (بودجه):** `MaintenanceWatchdog` با `periodic_ms=1000` و `ops_period=3` → بودجه‌ی زمانی (`ResetReason::Periodic`) → بودجه‌ی عملیات (`ResetReason::OpsBudget`) → درخواست دستی (`ResetReason::Manual`).
8. **بررسی سلامت پس از بازنشانی:** `maintenance().start_check()` → پاسخ به `AT+CSQ`/`AT+CPIN?`/`AT+CREG?` → `health().at_ok` و `rssi_dbm == -113 + 2*20`.
9. **حذف SIM:** تزریق `+CPIN: NOT INSERTED` → `ModemState::SimMissing` + رویداد `SimState::Removed`.
10. **بازیابی SIM با چرخه‌ی CFUN:** مشاهده‌ی `AT+CFUN=0` و `AT+CFUN=1` → تزریق `+CPIN: READY` → `ModemState::WaitingRegistration`.
11. **واچ‌داگ و پاک‌سازی صف:** `pause_queue` → شغل SMS → جلو بردن ۴۶ ثانیه (بیش از `GSM_JOB_MAX_AGE_MS=45000`) → صف خالی (`queue_size()==0`).
12. **واچ‌داگ تایم‌اوت‌های متوالی:** سه بار ارسال ناموفق → `AT+CFUN=1,1`؛ اگر CFUN جواب نگیرد، پروب‌های بوت شکست می‌خورند و PWRKEY به‌عنوان پشتیبان زده می‌شود.
13. **پولس غیرمسدودکننده‌ی PWRKEY + boot guard:** `power_cycle()` بلافاصله برمی‌گردد (پین را خودش دست نمی‌زند)، `tick()` پین را پایین می‌کشد، ۱۲۰۰ms نگه می‌دارد، رها می‌کند و تا پایان `GSM_MODEM_BOOT_GUARD_MS` هیچ `AT` نمی‌فرستد؛ سپس مودم تا `Ready` بالا می‌آید.
14. **پاسخ‌دادن مجدد به ماژول خاموش‌شده:** با سکوت ماژول، کتابخانه خودش پولس بعدی را می‌زند (off → on) و پس از پاسخ `OK` وضعیت به `WaitingRegistration` می‌رود؛ پین PWRKEY هیچ‌وقت asserted باقی نمی‌ماند.
15. **`NORMAL POWER DOWN` کهنه بعد از `AT`:** تزریق URC بعد از Ready → وضعیت `Off` نمی‌شود.
16. **قطع غیرمنتظره بعد از پنجرهٔ ignore:** `POWERED DOWN` → بازیابی با `power_cycle` (نه ماندن در `Off`).
17. **`POWER DOWN` حین boot guard:** پولس دوم (روشن کردن) زده می‌شود؛ URC دیررس بعد از `AT` نادیده گرفته می‌شود.
18. **`POWER DOWN` دو تکه وسط پروب `AT`:** `NORMAL POWER DO` + `WN` نباید توسط `execute` دور ریخته شود.
19. **ریست نرم `AT+CFUN=1,1`:** `soft_reboot()` فرمان را می‌فرستد، بعد از `OK` boot guard، سپس پروب `AT`.

در پایان `"ALL TESTS PASSED!"` چاپ می‌شود.

## نکات فنی

- تست‌ها قطعی هستند؛ زمان با `advance` و پاسخ‌ها با `uart.inject` کنترل می‌شوند (بدون خواب واقعی).
- برای ترافیک AT قطعی، `housekeeping().enabled=false` و `maintenance().policy().health_check=false` در بخش‌های مربوطه ست می‌شوند.
- نگهبان طول پیامک: `kErrSmsTooLong` باید در `JobCallback` گزارش شود و هیچ `AT+CMGS` ارسال نشود؛ محدودیت با `modem.sms().set_max_chars` در زمان اجرا تغییر می‌کند.
- نظارت تماس: قطع خودکار پس از ~۲۰ ثانیه؛ حالت `+CLCC: 1,0,3,0,0` (زنگ/هشدار) نگه داشته می‌شود تا سرانجام `ATH`/`AT+CHUP`.
- دروازه‌ی صف: در حین تماس، شغل‌های SMS ارسال نمی‌شوند و پس از پایان تماس دوباره فعال می‌شوند.
- URC پایان تماس از‌دست‌رفته: مودم‌هایی مثل M08R `NO CARRIER`/`BUSY` نمی‌فرستند؛ لیست خالی `+CLCC` باید پایان تماس را آشکار کند.
- بازنشانی نگهداری: هم بودجه‌ی زمانی (`periodic_ms`) و هم بودجه‌ی عملیات (`ops_period`) باید مستقل کار کنند.
- سلامت: `rssi_dbm` از `+CSQ: 20,0` با فرمول `-113 + 2*rssi` محاسبه می‌شود.
- بازیابی SIM: چرخه‌ی `AT+CFUN=0/1` (بر مبنای فرمان‌های مشاهده‌شده، نه زمان) و سپس `+CPIN: READY`.
- پاک‌سازی صف: شغل‌های کهنه‌تر از `GSM_JOB_MAX_AGE_MS` (۴۵۰۰۰ms) توسط واچ‌داگ حذف می‌شوند.
- واچ‌داگ هنگ: تایم‌اوت‌های متوالی ارسال → `AT+CFUN=1,1`، سپس در صورت سکوت ماژول پولس PWRKEY.

## ارتباط با سایر فایل‌ها

- `gsm/ports/host/gsm_port_host.hpp` — شبیه‌ساز UART/Clock/Power که تست بر پایه‌ی آن است.
- `gsm/modem/modem.hpp` — API اصلی زیر تست (`Modem`، `dial`، `send_sms`، `check_balance`، صف، نگهداری).
- `gsm/codec/ucs2.hpp` — `codec::utf8_to_ucs2_hex` برای تست کدک.
- `gsm/CMakeLists.txt` — با `GSM_BUILD_TESTS` هدف `host_smoke` را می‌سازد و با `add_test` ثبت می‌کند.
- پیکربندی‌ها/ثابت‌ها: `GSM_SMS_MAX_CHARS`، `GSM_JOB_MAX_AGE_MS` (از `gsm/gsm_config.hpp`).
- کلاس‌های `JobCallback`، `JobState`، `CallState`، `CallEndReason`، `HealthState`، `ResetReason` از هسته‌ی کتابخانه.
