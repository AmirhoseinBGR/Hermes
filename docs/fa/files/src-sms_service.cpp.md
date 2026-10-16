# gsm/src/sms_service.cpp

## نقش فایل

پیاده‌سازی `gsm::SmsService`؛ ماشین وضعیت چندمرحله‌ای برای ارسال، خواندن، اسکن و حذف پیامک در حالت متنی (`AT+CMGF=1`). سرویس تنظیمات جلسه (متن‌بودن، charset، پارامترهای CSMP، مخزن پیامک) را کش می‌کند تا فرمان‌های تکراری نفرستد، رمزنگاری UCS2/GSM7 را خودکار انتخاب می‌کند، و در نسخه v0.4 یک «محافظ سخت‌گیرانه طول متن خروجی» دارد: متن‌های بلندتر از `max_chars` (پیش‌فرض ۶۰ نویسه UTF-8) همان اول کار با کد خطای `kErrSmsTooLong` رد می‌شوند تا مودم‌های خانواده M08R که با payload بزرگ هنگ می‌کنند یا پیامک خالی می‌فرستند، از ریشه در امان بمانند.

## توابع و منطق کلیدی

### `to_string(SmsStep)`
نام گام‌ها برای لاگ/diag: `"cmgf" | "cscs" | "csmp" | "cpms" | "cmgs-header" | "payload" | "wait-cmgs" | "cmgl" | "cmgr" | "cmgd" | "cmgd-read" | …`

### `uint16_t SmsService::count_chars(const char* utf8)` — (ایستا)
تعداد «نقاط کد» UTF-8 را می‌شمارد (چیزی که کاربر به‌عنوان کاراکتر می‌بیند)، نه بایت‌ها:

```cpp
for (const unsigned char* p = ...; *p; ++p)
    if ((*p & 0xC0) != 0x80) ++n; // count lead bytes only
```

فقط بایت‌های آغازین (lead bytes) شمرده می‌شوند؛ بایت‌های ادامه (continuation) با ماسک `10xxxxxx` حذف می‌شوند. این همان متریکی است که `max_chars` با آن مقایسه می‌شود.

### `bool SmsService::start_send(const char* number, const char* utf8_text, SmsEncoding enc)`
شروع ارسال. ترتیب اعتبارسنجی:

1. اگر سرویس مشغول باشد → `false`.
2. **محافظ طول (v0.4):** اگر `max_chars_ != 0` و `count_chars(utf8_text) > max_chars_` → لاگ خطا، `result_ = AtResult{AtStatus::Error, kErrSmsTooLong}` و `false`.
3. شماره گیرنده باید با `PhoneNumber::assign` معتبر شود.
4. انتخاب رمزنگاری: `Auto` → `default_enc_`؛ اگر GSM7 انتخاب شده باشد ولی متن غیر-ASCII باشد، با هشدار به UCS2 ارتقا داده می‌شود.
5. گزارش ارسال (`SmsSendReport`) ریست و به گام `SetTextMode` می‌رود.

### `bool SmsService::start_read(int16_t index, bool delete_after)` / `start_scan_unread()` / `start_delete_index(int16_t)` / `start_delete_read()`
ورودی‌های دیگر سرویس. `start_read` همیشه UCS2 می‌خواند؛ `start_scan_unread` شمارنده `pending_count_` را صفر می‌کند؛ نسخه‌های حذف مستقیم به گام `DeleteIndex`/`DeleteRead` می‌پرند.

### `void SmsService::queue_index(int16_t index)`
ایندکس پیامک خوانده‌نشده را در بافر `pending_` (حداکثر `GSM_SMS_INBOX_SLOTS = 4`) صف می‌کند.

### `void SmsService::cancel()` / `fail(...)` / `complete()`
- `cancel` — لغو ESC-safe: `at_.abort()` (ارسال ESC اگر در پرامپت CMGS باشیم)، نتیجه `Aborted`.
- `fail(st, code)` — ثبت نتیجه در `result_` و `report_.result`، بازگشت به `Idle` و **باطل‌سازی جلسه** با `session_.invalidate()`؛ چون بعد از خطا وضعیت charset/پرامپت مودم ممکن است نامشخص باشد و کش جلسه نباید به حافظه‌اش اعتماد کند.
- `complete` — نتیجه موفق `Ok`.

### `void SmsService::pump()`
حلقه‌ای با حداکثر ۸ پله `advance_step()`؛ گام‌هایی که به لطف کش جلسه فوراً skip می‌شوند در همان فراخوانی زنجیر می‌خورند بدون اینکه نوبت tick بعدی منتظر بمانند:

```cpp
for (uint8_t guard = 0; guard < 8; ++guard)
{
    const SmsStep before = step_;
    advance_step();
    if (issued_ || !active() || step_ == before) return;
}
```

### `void SmsService::advance_step()` — ماشین گام‌ها
مسیر مشترک تنظیمات (با کش جلسه):

| گام | فرمان | شرط skip |
|---|---|---|
| `SetTextMode` | `AT+CMGF=1` | `session_.text_mode` |
| `SetCharset` | `AT+CSCS="UCS2"` یا `"GSM"` | `session_.charset` برابر مقدار مطلوب |
| `SetCsmp` | `traits_.csmp_ucs2` / `traits_.csmp_gsm` | `session_.csmp_ucs2` هم‌فاز و charset شناخته‌شده |
| `SetStorage` | `traits_.storage_cmd` | `session_.storage_ready` یا بدون دستور مخزن |

سپس انشعاب بر اساس هدف: `target_index_ >= 0` → `ReadMessage`؛ گیرنده نامعتبر → `ListUnread`؛ در غیر این صورت → `SendHeader`.

**مرحله `SendHeader`:** شماره مقصد در صورت نیاز دستگاه (`traits_.ucs2_recipient_required`) با `codec::ascii_to_ucs2_hex` به هگز UCS2 تبدیل می‌شود، سپس:

```cpp
std::snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", dest);
req.expect_prompt = true;                 // wait for '>' prompt
req.timeout_ms    = traits_.prompt_timeout_ms;
```

**مرحله `WritePayload`:** متن UCS2 (با `utf8_to_ucs2_hex`) یا خام نوشته می‌شود، بلافاصله `at_.write_ctrl_z()` پایان payload را اعلام می‌کند و با `at_.await_final(traits_.send_timeout_ms, this)` منتظر `+CMGS:` / `OK` می‌ماند. در شکست رمزنگاری، ابتدا `write_escape()` و سپس `fail` اجرا می‌شود.

**مراحل خواندن/حذف:** `AT+CMGL="REC UNREAD"` (۱۵s)، `AT+CMGR=%d` (۵s)، `AT+CMGD=%d` (۵s) و `AT+CMGD=1,1` (یا `AT+CMGD=1` بسته به `traits_.supports_cmgd_delete_all`).

### `void SmsService::on_prompt()`
دیدن پرامپت `>` مودم: اگر در `SendHeader` باشیم یعنی مودم آماده دریافت payload است → گام `WritePayload`.

### `void SmsService::parse_cmgr_header(const char* line)` / `store_body(const char* line)`
هدر `+CMGR:` (تک‌پیام) و `+CMGL:` (فهرست) با ایندکس فیلد متفاوت پارس می‌شوند (فهرست یک فیلد ایندکس اضافه دارد). شماره فرستنده اگر هگز UCS2 به‌نظر برسد (`looks_like_ucs2_hex`) رمزگشایی می‌شود. بدنه پیام در خط بعدی با `store_body` ذخیره و رمزنگاری واقعی (UCS2 یا GSM7) با بو کردن محتوا تشخیص داده می‌شود.

### `void SmsService::on_line(const char* line, uint16_t len)`
- `+CMGS: <mr>` → شماره مرجع ارسال در `report_.reference`.
- `+CMGR:` / `+CMGL:` → `parse_cmgr_header`.
- `+CPMS:` نادیده گرفته می‌شود (وضعیت مخزن را `StatusService` مدیریت می‌کند).
- خط بدنه (وقتی `expect_body_`) → `store_body`؛ در حالت `ListUnread` هر پیام بلافاصله با `queue_index` + `on_received_(last_)` تحویل داده می‌شود.

### `void SmsService::on_final(const AtResult& res)`
در هر نتیجه غیرموفق اگر در فاز payload باشیم (`WritePayload`/`WaitAck`) ابتدا `at_.write_escape()` ارسال می‌شود تا مودم از حالت ورود داده درنیاید، سپس `fail`. در موفقیت، پرچم‌های جلسه (`text_mode`، `charset`، `csmp_ucs2`، `storage_ready`) ثبت و زنجیره ادامه می‌یابد؛ پس از `SetStorage` دوباره به همان گام برمی‌گردد تا انشعاب نهایی (ارسال/خواندن/فهرست) انتخاب شود. `SendHeader` بدون دریافت پرامپت → `fail`. `WaitAck` موفق → `complete()` با لاگ `sent, ref=%d`. در `ReadMessage` اگر `delete_after_` تنظیم باشد، به‌طور خودکار به `DeleteIndex` زنجیر می‌شود.

## جریان کاری

```text
start_send(num, text)
   │  guard: count_chars(text) > max_chars ? → kErrSmsTooLong (رد فوری)
   ▼
SetTextMode ─► SetCharset ─► SetCsmp ─► SetStorage ─► SendHeader
   (CMGF=1)      (CSCS)       (CSMP)     (CPMS)        │ AT+CMGS="…" expect_prompt
                                                       ▼
                                              on_prompt('>') ─► WritePayload
                                                       │  write_raw(hex|text) + Ctrl+Z
                                                       ▼
                                              WaitAck ── await_final(send_timeout)
                                                       │  +CMGS: <mr> … OK
                                                       ▼
                                                   complete()

start_scan_unread: … ─► ListUnread (CMGL) ─► هر پیام: store_body → queue_index + on_received_
start_read(idx):   … ─► ReadMessage (CMGR) ─► [delete_after?] ─► DeleteIndex
```

## نکات فنی

- **`max_chars` دو لایه اجرا می‌شود:** هم در `Modem::send_sms` (رد قبل از ورود به صف) و هم در `SmsService::start_send` (رد هنگام اجرا). مقدار پیش‌فرض `GSM_SMS_MAX_CHARS = 60` با هدف حاشیه امن زیر سقف ۷۰ نویسه یک SMS UCS2 تک‌بخشی انتخاب شده؛ با `set_max_chars` (دستور کنسولی `smsmax`) یا مقدار ۰ (غیرفعال) قابل تنظیم است.
- **شمارش «نویسه» نه «بایت»:** متن فارسی ۶۰ نویسه‌ای ممکن است ۱۲۰ بایت باشد؛ `count_chars` بر مبنای نقاط کد UTF-8 تصمیم می‌گیرد.
- **رفتار `fail`:** چون ممکن است مودم در وضعیت نیمه‌کاره (مثلاً پرامپت CMGS) رها شده باشد، جلسه کاملاً باطل می‌شود تا گام‌های تنظیمی دوباره و بدون اتکا به کش ارسال شوند.
- **`pump` زنجیره‌ای:** گام‌های skip‌شونده حداکثر ۸ پله در یک نوبت جلو می‌روند؛ این تأخیر tick را حذف می‌کند ولی جلوی حلقه بی‌نهایت را هم می‌گیرد.
- **`await_final` به‌جای `execute`:** در فاز payload هیچ فرمان جدیدی ارسال نمی‌شود؛ فقط نتیجه نهایی (OK/+CMGS) انتظار کشیده می‌شود. این از تداخل با guard-time موتور AT هم جلوگیری می‌کند.
- **بدون heap:** بافرهای موقت (`dest`، `hex`) همگی آرایه‌های ثابت با اندازه‌های `GSM_SMS_TEXT_BYTES` / `GSM_SMS_NUMBER_BYTES` هستند.
- **`kErrSmsTooLong = -100`:** کد خطای داخلی کتابخانه (نه کد CME/CMS) و در `JobResult` قابل تشخیص است؛ `Modem::finish_job` چنین خطاهای سیاستی را retry نمی‌کند.

## ارتباط با سایر فایل‌ها

- `gsm/sms/sms_service.hpp` + `gsm/sms/sms_types.hpp` — `SmsStep`، `SmsEncoding`، `SmsMessage`، `SmsSendReport`، `max_chars_`.
- `gsm/core/at_engine.hpp` — `execute`، `await_final`، `write_raw`، `write_ctrl_z`، `write_escape`، `abort`.
- `gsm/core/at_types.hpp` — `AtResult` و `kErrSmsTooLong`.
- `gsm/codec/ucs2.hpp` + `src/ucs2.cpp` — تبدیل‌های UTF-8 ↔ هگز UCS2 و تشخیص `looks_like_ucs2_hex`.
- `gsm/codec/phone_number.hpp` — اعتبارسنجی شماره گیرنده.
- `gsm/core/service.hpp` — رابط `IService` (پیاده‌سازی `pump/cancel/on_line/on_final`).
- `src/modem.cpp` — صف‌بندی jobهای `SmsSend/SmsRead/SmsScan/SmsDelete*` و فراخوانی `start_*`.
- `include/gsm/gsm_config.hpp` — `GSM_SMS_MAX_CHARS`، `GSM_SMS_TEXT_BYTES`، `GSM_SMS_NUMBER_BYTES`، `GSM_SMS_INBOX_SLOTS`.
