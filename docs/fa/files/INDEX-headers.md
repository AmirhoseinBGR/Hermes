# فهرست مستندات فایل های هدر کتابخانهٔ GSM (فارسی)

مستندسازی تک تک فایل های `gsm/include/gsm/` — یک فایل Markdown به ازای هر هدر. نام فایل ها با اتصال بخش های مسیر با `-` ساخته شده اند.

| مسیر فایل | خلاصه |
|---|---|
| `gsm_config.hpp` | مرکز پیکربندی کتابخانه: ماکروهای بافر، timeout، نگهبان طول پیامک، نظارت تماس و بازنشانی های تعمیراتی |
| `hal/hal.hpp` | لایهٔ انتزاع سخت افزار: اینترفیس های `IUart`، `IClock`، `IPowerControl` و تابع مقاوم به سرریز `elapsed` |
| `core/at_types.hpp` | انواع پایهٔ AT: `AtStatus`، `AtResult`، کدهای خطای `kErr*` و اینترفیس `IAtObserver` |
| `core/at_engine.hpp` | موتور اجرای فرمان های AT: ارسال/دریافت، تشخیص prompt و خطوط نهایی، timeout و لغو تراکنش |
| `core/line_reader.hpp` | تبدیل جریان بایت به خطوط AT و نشان `>` با آشکارسازی overflow |
| `core/service.hpp` | قرارداد مشترک سرویس ها (`IService`) و وضعیت نشست مشترک (`SessionState`) |
| `core/urc_router.hpp` | طبقه بندی و مسیریابی پیام های ناخواسته (URC) مثل RING و +CMTI به handler |
| `device/device_traits.hpp` | توصیف ماژول های SIM800/M08/M08R؛ ویژگی v0.4: `clcc_authoritative` برای M08R |
| `log/log.hpp` | سامانهٔ لاگ سبک وزن: سطح ها، کانال های تگ دار، sinkهای قابل تعویض و `ScopedSink` |
| `queue/job.hpp` | ساختار `Job` (واحد کاری صف)، `JobKind`/`JobState` و فیلد `deferred` برای وقفهٔ تماس |
| `queue/job_queue.hpp` | صف FIFO ایستا با لغو، تعلیق و `purge_stale` برای جلوگیری از انباشت صف |
| `sms/sms_service.hpp` | سرویس پیامک: ارسال/خواندن/حذف؛ ویژگی v0.4: نگهبان سخت گیرانهٔ طول (`max_chars`، `kErrSmsTooLong`) |
| `sms/sms_types.hpp` | انواع دادهٔ پیامک: `SmsEncoding`، `SmsMessage`، `SmsSendReport` و صندوق حلقوی `SmsInbox` |
| `status/status_service.hpp` | سرویس وضعیت مودم: سیگنال، ثبت شبکه، SIM، ذخیره سازی و اپراتورهای ایرانی در `ModemStatus` |
| `tools/serial_bridge.hpp` | پل سریال برای اتصال کنسول به مودم: حالت managed (خط فرمان) و passthrough (بایت خام) |
| `ussd/ussd_service.hpp` | سرویس USSD با نشست تعاملی (`send_reply`/`end_session`) و پرس وجوی خودکار موجودی |
| `util/delegate.hpp` | callback سبک وزن بدون heap (`Delegate`) با bind ایمن به متدهای عضو |
| `util/ring_buffer.hpp` | بافر حلقوی بایتی ایستا با پرچم overflow؛ مناسب بافر UART در حالت وقفه |
| `util/static_string.hpp` | رشتهٔ با اندازهٔ ثابت `StaticString<N>` و ابزارهای پارس AT (`parse_int`، `csv_field`) |
| `voice/call_service.hpp` | سرویس تماس با نظارت v0.4: `CallPolicy`، `CallEndReason`، قطع/رد خودکار و polling ‏`+CLCC` |
| `modem/modem.hpp` | کلاس اصلی `Modem`: چرخهٔ عمر، صف Jobها، housekeeping، بازیابی SIM و gate تماس |
| `codec/phone_number.hpp` | نرمال سازی شماره تلفن به E.164 با کد کشور پیش فرض `+98` |
| `codec/ucs2.hpp` | تبدیل های UTF-8 ↔ hex-UCS2 برای پیامک فارسی و تشخیص رمزگذاری |
| `cli/cli.hpp` | کنسول تعاملی `cli::Console` (منتقل شده از برنامه به هستهٔ کتابخانه) با منو، وضعیت زنده و خروجی وب |
| `maintenance/watchdog.hpp` | نگهبان تعمیراتی v0.4: دو بازنشان (دوره ای + بودجهٔ عملیات)، `ResetPolicy` و بررسی سلامت |

## فهرست فایل های تولیدشده

- `include-gsm-gsm_config.hpp.md`
- `include-gsm-hal-hal.hpp.md`
- `include-gsm-core-at_types.hpp.md`
- `include-gsm-core-at_engine.hpp.md`
- `include-gsm-core-line_reader.hpp.md`
- `include-gsm-core-service.hpp.md`
- `include-gsm-core-urc_router.hpp.md`
- `include-gsm-device-device_traits.hpp.md`
- `include-gsm-log-log.hpp.md`
- `include-gsm-queue-job.hpp.md`
- `include-gsm-queue-job_queue.hpp.md`
- `include-gsm-sms-sms_service.hpp.md`
- `include-gsm-sms-sms_types.hpp.md`
- `include-gsm-status-status_service.hpp.md`
- `include-gsm-tools-serial_bridge.hpp.md`
- `include-gsm-ussd-ussd_service.hpp.md`
- `include-gsm-util-delegate.hpp.md`
- `include-gsm-util-ring_buffer.hpp.md`
- `include-gsm-util-static_string.hpp.md`
- `include-gsm-voice-call_service.hpp.md`
- `include-gsm-modem-modem.hpp.md`
- `include-gsm-codec-phone_number.hpp.md`
- `include-gsm-codec-ucs2.hpp.md`
- `include-gsm-cli-cli.hpp.md`
- `include-gsm-maintenance-watchdog.hpp.md`
