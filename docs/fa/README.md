# راهنمای فایل‌به‌فایل مستندات (فارسی)

برای **تک‌تک فایل‌های کد** کتابخانه GSM یک راهنمای مستقل نوشته شده است.
هر سند شامل: نقش فایل، ساختارها/توابع کلیدی، جریان کاری، نکات فنی و ارتباط با سایر فایل‌هاست.

نسخه HTML همه این اسناد در [`../html/`](../html/) تولید می‌شود (`python3 docs/build_html.py`).


## هدرها (`include/gsm/`)

| مسیر فایل | خلاصه |
|---|---|
| `gsm_config.hpp` | مرکز پیکربندی کتابخانه: ماکروهای بافر، timeout، نگهبان طول پیامک، نظارت تماس و بازنشانی‌های تعمیراتی |
| `hal/hal.hpp` | لایهٔ انتزاع سخت‌افزار: اینترفیس‌های `IUart`، `IClock`، `IPowerControl` و تابع مقاوم به سرریز `elapsed` |
| `core/at_types.hpp` | انواع پایهٔ AT: `AtStatus`، `AtResult`، کدهای خطای `kErr*` و اینترفیس `IAtObserver` |
| `core/at_engine.hpp` | موتور اجرای فرمان‌های AT: ارسال/دریافت، تشخیص prompt و خطوط نهایی، timeout و لغو تراکنش |
| `core/line_reader.hpp` | تبدیل جریان بایت به خطوط AT و نشان `>` با آشکارسازی overflow |
| `core/service.hpp` | قرارداد مشترک سرویس‌ها (`IService`) و وضعیت نشست مشترک (`SessionState`) |
| `core/urc_router.hpp` | طبقه‌بندی و مسیریابی پیام‌های ناخواسته (URC) مثل RING و +CMTI به handler |
| `device/device_traits.hpp` | توصیف ماژول‌های SIM800/M08/M08R؛ ویژگی v0.4: `clcc_authoritative` برای M08R |
| `log/log.hpp` | سامانهٔ لاگ سبک‌وزن: سطح‌ها، کانال‌های تگ‌دار، sinkهای قابل تعویض و `ScopedSink` |
| `queue/job.hpp` | ساختار `Job` (واحد کاری صف)، `JobKind`/`JobState` و فیلد `deferred` برای وقفهٔ تماس |
| `queue/job_queue.hpp` | صف FIFO ایستا با لغو، تعلیق و `purge_stale` برای جلوگیری از انباشت صف |
| `sms/sms_service.hpp` | سرویس پیامک: ارسال/خواندن/حذف؛ ویژگی v0.4: نگهبان سخت‌گیرانهٔ طول (`max_chars`، `kErrSmsTooLong`) |
| `sms/sms_types.hpp` | انواع دادهٔ پیامک: `SmsEncoding`، `SmsMessage`، `SmsSendReport` و صندوق حلقوی `SmsInbox` |
| `status/status_service.hpp` | سرویس وضعیت مودم: سیگنال، ثبت شبکه، SIM، ذخیره‌سازی و اپراتورهای ایرانی در `ModemStatus` |
| `tools/serial_bridge.hpp` | پل سریال برای اتصال کنسول به مودم: حالت managed (خط‌فرمان) و passthrough (بایت خام) |
| `ussd/ussd_service.hpp` | سرویس USSD با نشست تعاملی (`send_reply`/`end_session`) و پرس‌وجوی خودکار موجودی |
| `util/delegate.hpp` | callback سبک‌وزن بدون heap (`Delegate`) با bind ایمن به متدهای عضو |
| `util/ring_buffer.hpp` | بافر حلقوی بایتی ایستا با پرچم overflow؛ مناسب بافر UART در حالت وقفه |
| `util/static_string.hpp` | رشتهٔ با اندازهٔ ثابت `StaticString<N>` و ابزارهای پارس AT (`parse_int`، `csv_field`) |
| `voice/call_service.hpp` | سرویس تماس با نظارت v0.4: `CallPolicy`، `CallEndReason`، قطع/رد خودکار و polling ‏`+CLCC` |
| `modem/modem.hpp` | کلاس اصلی `Modem`: چرخهٔ عمر، صف Jobها، housekeeping، بازیابی SIM و gate تماس |
| `codec/phone_number.hpp` | نرمال‌سازی شماره تلفن به E.164 با کد کشور پیش‌فرض `+98` |
| `codec/ucs2.hpp` | تبدیل‌های UTF-8 ↔ hex-UCS2 برای پیامک فارسی و تشخیص رمزگذاری |
| `cli/cli.hpp` | کنسول تعاملی `cli::Console` (منتقل‌شده از برنامه به هستهٔ کتابخانه) با منو، وضعیت زنده و خروجی وب |
| `maintenance/watchdog.hpp` | نگهبان تعمیراتی v0.4: دو بازنشان (دوره‌ای + بودجهٔ عملیات)، `ResetPolicy` و بررسی سلامت |

## فایل‌های منبع (`src/`)

| مسیر فایل | خلاصه |
|---|---|
| `gsm/src/at_engine.cpp` | موتور تک‌فرمانی AT: ارسال فرمان، دسته‌بندی نتایج نهایی (OK/ERROR/CME/CMS/NO CARRIER/…)، مدیریت پرامپت `>` و timeout — زیربنای همه سرویس‌ها. |
| `gsm/src/at_types.cpp` | تبدیل `AtStatus` به رشته و جدول‌های راهنمای خطاهای `+CMS ERROR` / `+CME ERROR` برای لاگ‌های قابل‌فهم. |
| `gsm/src/call_service.cpp` | مدیریت تماس (MO/MT) با سرپرستی v0.4: تایمرهای خودکار قطع/رد (پیش‌فرض ۲۰s)، تور ایمنی `+CLCC` برای تماس‌های بدون URC، گزارش `CallEndReason` و fallback به `ATH`. |
| `gsm/src/cli.cpp` | کنسول تعاملی `gsm::cli::Console` (منتقل‌شده از اپلیکیشن به کتابخانه): منو، وضعیت زنده، نمای صف، پنل watchdog، پل وب و حالت passthrough. |
| `gsm/src/job_queue.cpp` | صف حلقوی ثابت‌اندازه jobها با پشتیبانی urgent، لغو تکی/جمعی و پاک‌سازی jobهای کهنه — بدون heap. |
| `gsm/src/line_reader.cpp` | توکنایزر بایت‌به‌بایت جریان UART: خط کامل، پرامپت `>` و سرریز بافر. |
| `gsm/src/log.cpp` | زیرسیستم لاگ: کانال‌های چندسطحی، فرمت‌دهی printf-مانند با بافر ثابت، دامپ بایتی/هگز و سینک‌های قابل‌تعویض (stdio/null/writer). |
| `gsm/src/modem.cpp` | هسته مرکزی: FSM بوت/ثبت شبکه، صف jobها، و v0.4 شامل گیت تماس (حفظ صف حین تماس)، رفع گیرکردن `pump_services`، `preempt_for_call` (لغو ESC-safe پیامک)، `maintenance_service` و `full_restart`. |
| `gsm/src/phone_number.cpp` | نرمال‌سازی شماره تلفن به E.164 (بین‌المللی/داخلی/بدون صفر) با کد کشور پیش‌فرض `+98`. |
| `gsm/src/sms_service.cpp` | ماشین چندمرحله‌ای پیامک (ارسال/خواندن/اسکن/حذف) با کش جلسه، رمزنگاری خودکار UCS2/GSM7 و محافظ سخت‌گیرانه طول متن (`count_chars` / `max_chars` / `kErrSmsTooLong`). |
| `gsm/src/status_service.cpp` | جمع‌آوری وضعیت مودم (CSQ/CREG/CPIN/CPMS/COPS) در `ModemStatus` زنده + تشخیص اپراتورهای ایرانی (Irancell/MCI/RighTel). |
| `gsm/src/ucs2.cpp` | مبدل‌های UTF-8 ↔ هگز UCS2/UTF-16 با پشتیبانی جفت سورگیت، تشخیص هگز UCS2 و شمارش واحدها — پایه رمزنگاری پیامک/USSD. |
| `gsm/src/urc_router.cpp` | دسته‌بندی و مسیریابی پیام‌های ناخواسته مودم (RING/+CLIP/+CMTI/+CPIN/+CREG/+CUSD/…) به `Modem::handle_urc` بدون مزاحمت برای پاسخ فرمان‌ها. |
| `gsm/src/ussd_service.cpp` | سرویس USSD (استعلام موجودی اپراتورهای ایرانی): رمزنگاری UCS2، decode پاسخ `+CUSD` با dcs/بو کردن، timeout شبکه و بستن جلسه با `AT+CUSD=2`. |
| `gsm/src/watchdog.cpp` | watchdog نگهداری دوگانه v0.4: بازنشاننده بودجه زمانی (پیش‌فرض ۱ ساعت) + بودجه تعداد عملیات، و پروب‌های بررسی سلامت (AT/CPIN/CREG/CSQ) با خواندن `ModemStatus` زنده. |

## پورت‌ها، مثال‌ها، تست‌ها و فایل‌های ساخت

| مسیر فایل | خلاصه |
|---|---|
| `gsm/ports/arduino/gsm_port_arduino.hpp` | پُرت آردوینو: پیاده‌سازی `IUart`/`IClock` روی هر `Stream` سریال و `millis()` آردوینو. |
| `gsm/ports/host/gsm_port_host.hpp` | پُرت میزبان: شبیه‌ساز UART/Clock/Power با بافر تزریق/بازرسی برای تست‌های قطعی. |
| `gsm/ports/stm32/gsm_port_stm32.hpp` | پُرت STM32: UART مبتنی بر DMA، زمان‌سنج `HAL_GetTick` و کنترل PWRKEY/RESET مودم. |
| `gsm/user_code/cli.hpp` | هدر چسب نازک STM32 برای کنسول کتابخانه (مقداردهی، تیک، کال‌بک‌های UART). |
| `gsm/user_code/cli.cpp` | پیاده‌سازی چسب STM32: نمونه‌های UART/Clock/Power + مودم M08R + کنسول کتابخانه. |
| `gsm/examples/arduino_basic.ino` | نمونه‌ی مینیمال آردوینو: راه‌اندازی مودم SIM800، چاپ پیامک دریافتی و ارسال یک پیامک فارسی. |
| `gsm/examples/stm32_main.cpp` | نقطه‌ی ورود نمونه‌ی STM32: فراخوانی `AppInit`/`AppRun` و پل کال‌بک‌های HAL به ماژول `cli`. |
| `gsm/tests/host_smoke.cpp` | تست دود میزبان: کدک، بوت، USSD، نگهبان طول پیامک، نظارت تماس، صف، بازیابی SIM، واچ‌داگ. |
| `gsm/CMakeLists.txt` | پیکربندی CMake کتابخانه‌ی ایستای `gsm_modem` + گزینه‌های پُرت STM32 و تست میزبان. |
| `gsm/library.json` | متادیتای PlatformIO: نام/نسخه/توضیحات، پلتفرم‌ها/چارچوب‌ها و پرچم‌های ساخت C++17. |
| `App/gsm_cli/cli.hpp` | هدر چسب اپلیکیشن: کنسول کتابخانه + رابط پل وب/سریال (آینه‌ی لاگ، اجرای دستور، وضعیت زنده). |
| `App/gsm_cli/cli.cpp` | چسب STM32 + پل وب: سخت‌افزار، مودم M08R، حالت پاس‌ترو و forwarding متدهای وب به کنسول. |
| `App/CMakeLists.txt` | پیکربندی CMake لایه‌ی اپلیکیشن `app` (OBJECT) با وابستگی به `gsm_modem` و تنظیم سطح لاگ. |
