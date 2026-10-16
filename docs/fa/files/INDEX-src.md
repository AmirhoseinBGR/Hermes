# فهرست مستندات فایل‌های منبع (فارسی)

مستندات per-file برای هسته C++ کتابخانه مودم GSM — نسخه v0.4.
مسیرها نسبت به ریشه پروژه (`gsm/`) هستند.

| فایل | خلاصه |
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
