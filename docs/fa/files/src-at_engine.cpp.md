# gsm/src/at_engine.cpp

## نقش فایل

این فایل پیاده سازی `gsm::AtEngine` است؛ موتور اجرای فرمان های AT که به صورت غیرهمزمان (event-driven) روی یک رابط UART کار می کند. موتور فقط یک فرمان در حال اجرا (in-flight) را نگه می دارد، پاسخ ها را خط به خط از `LineReader` می گیرد، نتیجه نهایی (`OK`، `ERROR`، `+CME ERROR`، …) را دسته بندی می کند و از طریق رابط `IAtObserver` به سرویس فراخوان (SMS، تماس، وضعیت و …) اطلاع می دهد. تشخیص URCها (پیام های ناخواسته مودم) بر عهده `UrcRouter` است و این موتور فقط خط های باقی مانده را به observer می رساند.

## توابع و منطق کلیدی

### `bool AtEngine::execute(const AtRequest& req, IAtObserver* observer)`
شروع یک فرمان AT. اگر موتور مشغول باشد (`busy()`) یا هنوز زمان نگهبانی `GSM_AT_GUARD_TIME` (پیش فرض ۳۰ms) از پایان فرمان قبلی نگذشته باشد، `false` برمی گرداند. در حالت موفق، observer، مهلت (`timeout_ms`)، و انتظار پرامپت (`expect_prompt`) را ثبت می کند و فرمان را می فرستد (line reader ریست **نمی شود** تا URC نیمه کاره از بین نرود؛ تخلیهٔ عمدی با `discard_rx()`). فرمان به همراه `\r\n` روی UART می نویسد:

```cpp
if (busy()) return false;
if (!hal::elapsed(clock_.millis(), last_done_, GSM_AT_GUARD_TIME)) return false;
// ...
sent_.assign(req.command);
uart_.write(reinterpret_cast<const uint8_t*>(sent_.c_str()), sent_.size());
const uint8_t crlf[2] = {'\r', '\n'};
uart_.write(crlf, 2);
```

### `bool AtEngine::execute(const char* cmd, uint32_t timeout_ms, IAtObserver* observer)`
نسخه ساده شده که یک `AtRequest` موقت می سازد و به نسخه اصلی پاس می دهد.

### `bool AtEngine::await_final(uint32_t timeout_ms, IAtObserver* observer)`
بدون ارسال فرمان، فقط منتظر نتیجه نهایی می ماند. برای «فاز payload» کاربرد دارد (مثلاً بعد از نوشتن متن پیامک و Ctrl+Z که فقط باید `+CMGS: …` و `OK` دریافت شود). چون فرمانی ارسال نمی شود، `sent_` خالی می ماند و echo-killer غیرفعال است.

### `uint16_t AtEngine::write_raw(const uint8_t* data, uint16_t len)` / `write_raw(const char* text)`
نوشتن خام روی UART برای payloadها (هگز UCS2 یا متن خام). در حالت trace با پیشوند `TX<` لاگ بایتی می شود. نسخه `const char*` طول رشته را با `strlen` می گیرد و برای `nullptr` مقدار 0 برمی گرداند.

### `void AtEngine::discard_rx()`
`uart_.drain_rx()` + `reader_.reset()`؛ در شروع `power_cycle` تا `NORMAL POWER DOWN` باقی مانده از سیکل قبلی بعد از روشن شدن ظاهر نشود.

### `void AtEngine::abort()`
لغو فرمان در حال اجرا. اگر موتر در وضعیت انتظار پرامپت (`expect_prompt_`) باشد ابتدا `write_escape()` (ارسال ESC / بایت 0x1B) می فرستد تا مودم از حالت ورود داده خارج شود، سپس نتیجه را `AtStatus::Aborted` می کند. برای مودم هایی مثل M08R که با گیر کردن در پرامپت `>` مشکل ساز می شوند حیاتی است.

### `bool AtEngine::classify_final(const char* line, AtStatus& status, int16_t& code) const`
یک خط را در برابر پاسخ های نهایی استاندارد AT می سنجد و در صورت تطابق `true` برمی گرداند:

| خط پاسخ | `AtStatus` | کد |
|---|---|---|
| `OK` | `Ok` | -1 |
| `ERROR` | `Error` | -1 |
| `+CME ERROR: n` | `CmeError` | n |
| `+CMS ERROR: n` | `CmsError` | n |
| `NO CARRIER` | `NoCarrier` | -1 |
| `BUSY` | `Busy` | -1 |
| `NO ANSWER` | `NoAnswer` | -1 |
| `NO DIALTONE` | `NoDialtone` | -1 |

عدد خطا با `parse_int(line + 11, v)` (پس از پیشوند ۱۱ نویسه ای `"+CME ERROR:"` / `"+CMS ERROR:"`) استخراج می شود.

### `void AtEngine::finish(AtStatus status, int16_t code)`
پایان یک تراکنش: observer را پاک می کند (موتور دوباره `idle()` می شود)، زمان پایان را برای guard time ثبت می کند و `on_final(AtResult{status, code})` را روی observer قبلی فراخوانی می کند.

### `void AtEngine::tick()`
قلب موتور؛ در هر چرخه `Modem::tick` صدا زده می شود:

```text
while (uart_.read(&byte, 1) == 1)
    │
    ├─ LineReader::feed(byte)
    │     ├─ Token::Overflow → هشدار، reader_.reset() (خط دور ریخته می شود)
    │     ├─ Token::Prompt ('>') → observer_->on_prompt()
    │     │      و اگر expect_prompt_ باشد → finish(Ok)   ← پایان فاز هدر CMGS
    │     └─ Token::Line → پردازش خط کامل:
    │            ├─ خط اکوی فرمان ارسالی؟ → دور ریخته می شود
    │            ├─ classify_final()؟ → لاگ راهنمای CME/CMS + finish()
    │            ├─ urc_.dispatch()؟ → مصرف شد؛ به observer نمی رسد
    │            └─ در غیر این صورت → observer_->on_line(line, len)
    │
    └─ بررسی timeout: اگر observer فعال و مهلت سپری شده →
           هشدار، escape در صورت نیاز، finish(Timeout)
```

نکته ظریف: خطوط اکو (echo) با مقایسه دقیق با `sent_` حذف می شوند؛ برای همین `ATE0` در bootstrap اهمیت دارد ولی حتی با اکوی فعال هم موتور گمراه نمی شود.

## جریان کاری

```text
   سرویس (SMS/Call/…)                AtEngine                    مودم روی UART
   ──────────────────                ────────                    ──────────────
   execute("AT+CMGS=…")
        └────────────────────────►  ثبت observer/timeout
                                    write(cmd + CRLF) ──────────►  TX
   on_line(...)  ◄────────────────  reader_: خط از بایت ها        ◄── RX
   on_prompt()   ◄────────────────  دیدن '>' (پرامپت داده)
   write_raw(hex) / write_ctrl_z ─►  write_raw مستقیم ──────────►  TX payload
   await_final(timeout) ─────────►  انتظار نتیجه
   on_final(Ok/CmeError/…) ◄──────  finish() پس از OK/ERROR       ◄── RX
```

## نکات فنی

- **تک فرمانی:** هر لحظه فقط یک observer فعال است؛ `busy()` یعنی `observer_ != nullptr`. سرویس ها باید قبل از `execute` مطمئن شوند موتور آزاد است (خود موتور هم در صورت اشغال `false` برمی گرداند).
- **Guard time:** فاصله حداقل ۳۰ms بین دو فرمان (`GSM_AT_GUARD_TIME`) از تداخل پاسخ های مودم های کند جلوگیری می کند؛ یک `execute` ممکن است به ظاهر بی دلیل `false` برگرداند!
- **پرامپت `>`:** فقط وقتی بافر خط خالی باشد `Token::Prompt` تولید می شود؛ بنابراین `>` در میانه یک خط (مثلاً در متن پیامک) پرامپت تلقی نمی شود.
- **سرریز خط:** بافر `LineReader` با `GSM_LINE_MAX` (۵۱۲) محدود است؛ خطوط بلندتر با `Token::Overflow` بی سروصدا دور ریخته و ریست می شوند.
- **کدهای خطا:** `+CME/+CMS ERROR` هم به صورت عددی (`code`) و هم با متن راهنما (`cme_error_hint` / `cms_error_hint` از `at_types.cpp`) لاگ می شوند.
- **بدون تخصیص حافظه پویا:** همه بافرها ثابت و روی پشته/اعضای کلاس هستند؛ مناسب محیط های embedded.
- **Timeout در فاز payload:** وقتی `sent_` خالی است (یعنی `await_final`)، پیام timeout با `(payload phase)` چاپ می شود تا اشکال زابی ساده تر باشد.

## ارتباط با سایر فایل ها

- `gsm/core/at_engine.hpp` — تعریف کلاس، `AtRequest`، `write_ctrl_z()` / `write_escape()` (اینلاین در هدر).
- `gsm/core/line_reader.hpp` + `src/line_reader.cpp` — توکنایزر خط/پرامپت.
- `gsm/core/at_types.hpp` + `src/at_types.cpp` — `AtStatus`، `AtResult` و راهنماهای خطای CME/CMS.
- `gsm/core/urc_router.hpp` + `src/urc_router.cpp` — مسیریابی خطوط ناخواسته (URC) قبل از رسیدن به observer.
- `gsm/util/static_string.hpp` — `str_starts_with`، `parse_int`.
- `gsm/hal/hal.hpp` — `IUart`، `IClock` و `hal::elapsed` (محاسبه timeoutها بدون نگرانی از سرریز شمارنده).
- مصرف کننده ها: `src/sms_service.cpp`، `src/call_service.cpp`، `src/status_service.cpp`، `src/ussd_service.cpp`، `src/watchdog.cpp` و `Modem` (همگی `IAtObserver` هستند).
