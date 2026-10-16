# gsm/src/ussd_service.cpp

## نقش فایل

پیاده‌سازی `gsm::UssdService`؛ سرویس کدهای USSD (مثل استعلام موجودی) با فرمان `AT+CUSD`. متن خروجی همیشه به هگز UCS2 کدبندی می‌شود، پاسخ شبکه به‌صورت URC ناخواسته (`+CUSD:`) دریافت و با توجه به `dcs` (یا بو کردن محتوا) به UTF-8 برگردانده می‌شود. نسخه فعلی «تعاملی» نیست: هر پاسخی (حتی درخواست ورودی بیشتر شبکه با `m=1`) با `AT+CUSD=2` بسته می‌شود؛ مسیر جلسه باز (`SessionOpen`/`send_reply`) در کد حاضر است ولی غیرفعال شده است.

## توابع و منطق کلیدی

### توابع متنی
`to_string(UssdStep)` → `"cusd-enable" | "cusd-send" | "cusd-reply" | "wait-cusd-urc" | "session-open" | "cusd-end"` (+ `"idle"/"done"`).

### توابع ایستای کمکی (anonymous namespace)
- **`encode_ussd_text(text, out, cap, dcs)`** — تبدیل UTF-8 → هگز UCS2 با `codec::utf8_to_ucs2_hex` و مقداردهی `dcs = 15`. (کامنت داخل کد تأکید می‌کند با dcs=15 هم کار می‌کند چون payload ذاتاً UCS2 hex است.)
- **`build_cusd_command(text, cmd, cap)`** — ساخت فرمان کامل و رد در صورت شکست رمزنگاری یا بریده‌شدن:

```cpp
std::snprintf(cmd, cmd_cap, "AT+CUSD=1,\"%s\",%u", payload, dcs);
return n > 0 && static_cast<uint16_t>(n) < cmd_cap;   // truncation check
```

- **`decode_ussd_text(raw, dcs, out)`** (قالبی/template) — رمزگشایی پاسخ: `dcs == 72` → UCS2، `dcs == 15` → خام، نامشخص → `looks_like_ucs2_hex`؛ یک **تور ایمنی** هم دارد: حتی اگر dcs بگوید خام، اگر payload آشکارا هگز UCS2 باشد UCS2 رمزگشایی می‌شود (مودم‌هایی با dcs گزارش‌شده غلط).

### `const char* UssdService::default_balance_code(Operator op)`
کد پیش‌فرض استعلام موجودی اپراتورهای ایرانی: Irancell → `*141*1#`، MCI → `*140*11#`، RighTel و ناشناخته → `*140#`. (از `StatusService::detect_operator` تغذیه می‌شود.)

### شروع/کنترل جلسه
- **`start_query(const char* ussd_code)`** — شروع استعلام؛ کد در `code_` ذخیره، `report_` ریست و به گام `SetUssdMode` می‌رود.
- **`start_balance_query(Operator)`** — نسخه ساده‌شده با کد پیش‌فرض.
- **`send_reply(const char* text)`** — ادامه جلسه تعاملی (فقط وقتی `session_open_`)؛ در نسخه فعلی عملاً غیرقابل‌دسترس است چون `session_open_` هرگز true نمی‌شود.
- **`end_session()`** — بستن مؤدبانه جلسه با `AT+CUSD=2` (گام `EndSession`).
- **`cancel()`** — لغو: ابتدا `AT+CUSD=2` خام (بدون فرمان AT ثبت‌شده) نوشته می‌شود تا جلسه شبکه بسته شود، سپس `at_.abort()` و `complete(Aborted, -1)`.

### `void UssdService::pump()` — ماشین گام‌ها
- **`SetUssdMode`** — دو فراخوانی پشت‌سرهم `execute` دارد (یکی از نکات مهم این نسخه، پایین‌تر):

```cpp
case UssdStep::SetUssdMode:
    // AT+CUSD=1 enables presentation of USSD responses
    issued_ = at_.execute("AT+CUSD=2", 1000, this);
    issued_ = at_.execute("AT+CSCS=\"UCS2\"", 1000, this);
    return;
```

- **`SendQuery` / `SendReply`** — ساخت فرمان با `build_cusd_command`؛ در شکست رمزنگاری `complete(Aborted, -2)`؛ وگرنه `AT+CUSD=1,"<hex>",15` با timeout ۱۰s.
- **`WaitUrc`** — انتظار برای `+CUSD` شبکه با مهلت `GSM_AT_USSD_TIMEOUT` (۱۰s)؛ در timeout، ابتدا `AT+CUSD=2` خام نوشته و سپس `complete(Timeout, -1)`.
- **`EndSession`** — `AT+CUSD=2` با timeout ۵s.

### `void UssdService::on_line(...)` / `notify_cusd(const char* line)`
هر خط `+CUSD:` (چه از URC و چه پاسخ فرمان) به `notify_cusd` می‌رود. ساختار: `+CUSD: <m>,"<str>",<dcs>`:
1. استخراج `m` (کد وضعیت) و `dcs` با `csv_field`.
2. رمزگشایی payload با `decode_ussd_text` به `report_.response`؛ `report_.status_code = m`.
3. اگر در گام انتظار نباشیم (`SendQuery`/`SendReply`/`WaitUrc`)، پاسخ ناخواسته نادیده گرفته می‌شود (رویداد ثبت ولی کامل نمی‌شود).
4. **`m == 1`** (شبکه ورودی بیشتر می‌خواهد): در این نسخه جلسه بسته و `complete(Ok, 0)` می‌شود — مسیر `SessionOpen`/`on_done_` کامنت شده است.
5. سایر مقادیر (`0` موفقیت، `2` قطع توسط شبکه و …): `AT+CUSD=2` خام + `complete(Ok, m)`.

### `void UssdService::on_final(const AtResult& res)`
- نتیجه «ول‌شده» (stray): اگر گام `Idle` یا `SessionOpen` باشد نادیده گرفته می‌شود (پاسخ URC زودتر از OK فرمان رسیده و تکلیف را روشن کرده است).
- خطا → `complete(res.status, res.code)`.
- `SetUssdMode` موفق → `SendQuery`.
- `SendQuery`/`SendReply` موفق → `WaitUrc` (ثبت `wait_start_`).
- `EndSession` موفق → `AT+CUSD=2` خام + `complete(Ok, 0)`.

## جریان کاری

```text
start_query("*141*1#")
   │
   ▼
SetUssdMode ── (AT+CUSD=2 + تلاش AT+CSCS="UCS2") ──► SendQuery
   │                                                    │ AT+CUSD=1,"<ucs2hex>",15
   ▼                                                    ▼
on_final(OK) ──────────────────────────────► WaitUrc (10s)
                                                   │
                     ┌─────────────────────────────┤
                     ▼                             ▼
              +CUSD: 1,… (ورودی بیشتر)     +CUSD: 0/2,… یا timeout
                     │                             │
                     ▼                             ▼
        AT+CUSD=2 + complete(Ok)     AT+CUSD=2 + complete(Ok|Timeout)
                     │
                     └─► on_done_(UssdResult{response, status_code, result})
```

## نکات فنی

- **رفتار `SetUssdMode` (نکته مهم این نسخه):** دو `execute` پشت‌سرهم داده می‌شود در حالی که `AtEngine` فقط یک فرمان در پرواز می‌پذیرد؛ فراخوانی دوم (وقتی اولی هنوز تمام نشده) `false` برمی‌گرداند و `issued_` را `false` می‌کند. عملاً فقط `AT+CUSD=2` به مودم می‌رود و فرمان `AT+CSCS="UCS2"` در این مسیر ارسال نمی‌شود (و اگر `pump` دوباره صدا زده شود، `execute`های بعدی هم تا پایان فرمان اول رد می‌شوند). خوشبختانه bootstrap مودم (`kBoot` در `modem.cpp`) از قبل `AT+CSCS="UCS2"` را اجرا کرده و payload همیشه هگز UCS2 است؛ اما ناسازگاری کامنت (`AT+CUSD=1`) با کد (`AT+CUSD=2`) هم قابل‌توجه است — کد فعلی عملاً «بستن جلسه» می‌فرستد.
- **`AT+CUSD=2` خام در چند مسیر:** در `cancel`، timeout `WaitUrc`، `m==1` و پایان `EndSession` نوشته می‌شود؛ چون این مسیرها «فرمان AT ثبت‌شده» ندارند، از `write_raw` استفاده شده تا ماشین گام‌ها به‌هم نریزد.
- **کدبندی همیشه UCS2:** برخلاف SMS (که GSM7/UCS2 انتخابی است)، کدها و پاسخ‌های USSD با dcs=15 و payload هگز UCS2 رد و بدل می‌شوند؛ برای متن فارسی پاسخ‌ها ضروری است.
- **کد خطای `-2`:** شکست رمزنگاری خروجی (متن نامعتبر UTF-8 یا کمبود بافر) با `complete(Aborted, -2)` گزارش می‌شود؛ این کد داخلی سرویس است نه CME/CMS.
- **پاسخ‌های ناخواسته:** `+CUSD` ممکن است خارج از درخواست ما بیاید (مثلاً پیامک تعاملی شبکه)؛ شرط گام در `notify_cusd` جلوی کامل‌شدن کاذب را می‌گیرد ولی `report_.response` با آخرین مقدار به‌روز می‌شود.
- **ظرفیت‌ها:** بافرهای `raw`/`payload` با `GSM_USSD_RESPONSE_BYTES` (۵۱۲) و `GSM_CMD_MAX` (۲۵۶) محدودند؛ `build_cusd_command` بریده‌شدن را صریح رد می‌کند.
- **بدون heap:** همه رشته‌ها (`code_`، `report_.response`) از نوع `static_string` عضو کلاس‌اند.

## ارتباط با سایر فایل‌ها

- `gsm/ussd/ussd_service.hpp` — `UssdStep`، `UssdResult`، `session_open_`، `on_done_`.
- `gsm/core/at_engine.hpp` — `execute`، `write_raw`، `abort`، `now`.
- `gsm/codec/ucs2.hpp` + `src/ucs2.cpp` — `utf8_to_ucs2_hex`، `ucs2_hex_to_utf8`، `looks_like_ucs2_hex`.
- `src/urc_router.cpp` — مسیریابی `+CUSD:` به‌عنوان `Urc::Ussd` → `Modem::handle_urc` → `notify_cusd`.
- `src/status_service.cpp` — `Operator` برای `default_balance_code`.
- `src/modem.cpp` — job `UssdQuery` و `check_balance`؛ کپی `ussd_response` در `finish_job`.
- `include/gsm/gsm_config.hpp` — `GSM_USSD_RESPONSE_BYTES`، `GSM_AT_USSD_TIMEOUT`، `GSM_CMD_MAX`.
