# gsm/src/call_service.cpp

## نقش فایل

پیاده‌سازی `gsm::CallService`؛ ماشین وضعیت مدیریت تماس صوتی (MO/MT) روی مودم GSM. این فایل در نسخه v0.4 به «سرپرستی تماس» (call supervision) مجهز شده است: گام‌های شماره‌گیری/پاسخ/قطع (dial/answer/hangup) به‌صورت یک ماشین گام‌به‌گام (`CallStep`) اجرا می‌شوند، تایمرهای خودکار قطع تماس (`CallPolicy`، پیش‌فرض ۲۰ ثانیه) جلوی تماس‌های معلق را می‌گیرند، و یک «تور ایمنی» polling با `AT+CLCC` تماس‌هایی را که بدون URC قطع شده‌اند (مشکل شناخته‌شده خانواده M08R) شکار می‌کند. دلیل پایان هر تماس با `CallEndReason` گزارش می‌شود و در قطع تماس، در صورت شکست فرمان اختصاصی دستگاه، یک بار `ATH` به‌عنوان fallback امتحان می‌شود.

## توابع و منطق کلیدی

### توابع متنی
- `to_string(CallState)` → `"idle" | "dialing" | "outgoing" | "incoming" | "active" | "releasing"`
- `to_string(CallStep)` → `"idle" | "clip" | "atd" | "ata" | "hangup" | "hangup-fb" | "clcc" | "done"`
- `to_string(CallEndReason)` → `"none" | "local-hangup" | "remote-hangup" | "busy" | "no-answer" | "no-carrier" | "no-dialtone" | "timeout" | "error"`

### `void CallService::set_state(CallState s)`
گذار وضعیت با ثبت زمان (`state_since_ms_`)؛ هنگام ورود به `Active` زمان شروع مکالمه (`active_since_ms_`) ثبت می‌شود و هنگام بازگشت به `Idle` شمارنده‌های `+CLCC` (`clcc_misses_`، `clcc_seen_before_`) پاک می‌شوند. هر گذار، `on_state_(state, peer)` را فراخوانی می‌کند.

### `bool CallService::start_dial(const char* number)`
شروع تماس خروجی: فقط از `Idle` مجاز است؛ شماره با `PhoneNumber::assign` به E.164 نرمال می‌شود (شکست → `false`). نتیجه/دلیل/پرچم fallback ریست و `dial_since_ms_` ثبت می‌شود. اگر `auto_clip_` فعال باشد و `+CLIP` هنوز set نشده باشد، اولین گام `EnableClip` وگرنه مستقیم `Dial` است. وضعیت به `Dialing` می‌رود.

### `bool CallService::start_answer()`
پاسخ به تماس ورودی؛ فقط از `Incoming`. گام `Answer` (فرمان `ATA`) تنظیم می‌شود.

### `bool CallService::start_hangup()` / `void CallService::issue_hangup()`
درخواست قطع تماس. اگر فرمان AT دیگری در حال اجرا باشد ابتدا `at_.abort()` (لغو ESC-safe) صدا زده می‌شود؛ سپس گام `Hangup` و وضعیت `Releasing`. `cancel()` نیز در حالت غیر-idle همین مسیر را می‌رود.

### `void CallService::end_call(CallEndReason reason)`
پایان رسمی تماس: ثبت `end_reason_`، پاک کردن شماره طرف، ریست گام‌ها و شمارنده‌ها، گذار به `Idle` و فراخوانی `on_ended_(reason)`.

### `void CallService::pump()`
صدور فرمان AT متناظر با گام جاری (فقط وقتی فرمانی در پرواز نیست):

```cpp
case CallStep::EnableClip:     issued_ = at_.execute("AT+CLIP=1", 1000, this);
case CallStep::Dial:           // ATD<peer>;   — timeout 20s
case CallStep::Answer:         issued_ = at_.execute("ATA", 20000, this);
case CallStep::Hangup:         issued_ = at_.execute(traits_.hangup_cmd, 5000, this);
case CallStep::HangupFallback: issued_ = at_.execute("ATH", 5000, this);
```

فرمان قطع اولیه از `DeviceTraits` می‌آید (مثلاً `AT+CHUP` برای برخی چیپ‌ست‌ها) و فقط در صورت شکست `ATH` کلاسیک جایگزین می‌شود.

### `void CallService::monitor(uint32_t now_ms, bool engine_idle)` — سرپرستی خودران (v0.4)
در هر tick مودم — حتی وقتی هیچ job تماسی در صف نیست — اجرا می‌شود تا تماس ورودی بین jobهای پیامک هرگز صف را قفل نکند. دو لایه دارد:

**۱) تایمرهای خودکار قطع/رد تماس (`CallPolicy`):**

```cpp
// MO: تماس برقرار‌نشده ظرف dial_timeout_ms → قطع خودکار
if (mo_pending && hal::elapsed(now_ms, dial_since_ms_, policy_.dial_timeout_ms))
{ end_reason_ = CallEndReason::Timeout; start_hangup(); }
// MT: زنگ بی‌پاسخ ظرف ring_timeout_ms → رد خودکار
if (state_ == CallState::Incoming && hal::elapsed(now_ms, ring_since_ms_, policy_.ring_timeout_ms))
{ end_reason_ = CallEndReason::Timeout; start_hangup(); }
```

مقادیر پیش‌فرض: `dial_timeout_ms = ring_timeout_ms = 20000` (از `GSM_CALL_ANSWER_TIMEOUT_MS` / `GSM_CALL_RING_TIMEOUT_MS`)، `clcc_poll_ms = 2000`، `auto_hangup = true`. همه در زمان اجرا با `policy()` قابل تغییرند (دستور کنسولی `callto <ms>`).

**۲) تور ایمنی `+CLCC`:** فقط وقتی `traits_.supports_clcc`، موتور AT بیکار، و `clcc_poll_ms != 0` باشد؛ هر ۲ ثانیه یک `AT+CLCC` (بدون job، فقط probe) ارسال می‌شود.

### `void CallService::on_line(const char* line, uint16_t len)`
پاسخ‌های `+CLCC:` را می‌خواند: فیلد ۲ = وضعیت (`stat`)، فیلد ۵ = شماره (در صورت UCS2-hex بودن با `codec::ucs2_hex_to_utf8` رمزگشایی و در `peer_` ذخیره می‌شود):

| `stat` | برداشت | اقدام |
|---|---|---|
| 0 | فعال (پاسخ داده شده) | `set_state(Active)` |
| 2 / 3 | MO در حال شماره‌گیری/زنگ | اگر `Idle` → `Outgoing` |
| 4 / 5 | MT ورودی / انتظار | اگر `Idle` → `Incoming` + ثبت `ring_since_ms_` |

هر خط `+CLCC` دیده‌شده یعنی تماس هنوز در فهرست مودم است: `clcc_seen_ = clcc_seen_before_ = true` و `clcc_misses_ = 0`.

### `void CallService::on_final(const AtResult& res)` — قلب منطقی سرویس

**الف) اتمام یک probe مربوط به `+CLCC` (پرچم `probe_`):**
اگر تماسی دیده نشده باشد، فهرست خالی فقط وقتی «قابل‌اعتماد» است که: تماس قبلاً در `+CLCC` دیده شده باشد، یا وضعیت `Active` باشد، یا `traits_.clcc_authoritative` باشد، یا از گذار وضعیت بیش از `2 × clcc_poll_ms` گذشته باشد. سپس:

```cpp
if (trusted && ++clcc_misses_ >= GSM_CALL_CLCC_MISSES) // پیش‌فرض 2
{
    // Call is gone but no end-URC reached us (typical M08R failure)
    end_reason_ = ...RemoteHangup / NoAnswer...;
    start_hangup();   // آزادسازی فعال خط
}
```

**ب) ماشین گام‌ها:**
- `EnableClip` → نتیجه را در `clip_enabled_` ثبت، گام بعدی `Dial` (شکست CLIP مانع شماره‌گیری نمی‌شود).
- `Dial` + موفق → `Outgoing`، تازه‌سازی `dial_since_ms_`، `finish(Ok)`.
- `Dial` + شکست → نگاشت `AtStatus` به `CallEndReason` (`Busy`/`NoAnswer`/`NoCarrier`/`NoDialtone`/`Timeout`، پیش‌فرض `Error`) و `end_call(reason)`.
- `Answer` + موفق → `Active`؛ شکست → `end_call(Error)`.
- `Hangup` + شکست و fallback هنوز امتحان نشده → گام `HangupFallback` (یک بار `ATH`).
- `HangupFallback` یا `Hangup` موفق → `end_call(LocalHangup)` مگر اینکه دلیل قبلاً (مثلاً `Timeout` یا `RemoteHangup` از تور ایمنی) ثبت شده باشد.

### اعلان‌های URC (فراخوانی‌شده از `Modem::handle_urc`)
- `notify_ring(const char* caller)` — شماره تماس‌گیرنده را در `peer_` می‌نشاند؛ در `Idle` → `Incoming` + شروع تایمر زنگ؛ در `Incoming` → تازه‌سازی تایمر با هر burst زنگ (که زنگ‌های طولانی به‌اشتباه رد نشوند).
- `notify_hangup()` — برای `NO CARRIER`/`NO ANSWER`: اگر تماس خروجی در مراحل اولیه بود `NoAnswer`، وگرنه `RemoteHangup`.
- `notify_busy()` — برای `BUSY` → `end_call(Busy)`.

## جریان کاری

```text
start_dial(num)
    │  state=Dialing
    ▼
[EnableClip] ─AT+CLIP=1─► [Dial] ─ATD<num>;─► state=Outgoing
    │                                        │
    │   خطا: Busy/NoAnswer/NoCarrier/…        │  RING/+CLIP یا +CLCC stat=0
    │◄── end_call(reason) ────────────────► state=Active ──► (مکالمه)
    │                                        │
    │   تایمر 20s بی‌پاسخ / CLCC ناپدیدشده    │  NO CARRIER / notify_hangup
    ▼                                        ▼
[Hangup] ─traits_.hangup_cmd─► شکست؟ ──► [HangupFallback] ─ATH─►
    │                                              │
    └────────────► end_call(LocalHangup | Timeout | RemoteHangup | …)
                          │
                          ▼  state=Idle + on_ended_(CallEndReason)

تور ایمنی (همزمان، هر tick):
   هر clcc_poll_ms (2s) ──► AT+CLCC ──► فهرست خالی × 2 بار معتبر
                                        ──► end_reason ثبت + start_hangup
```

## نکات فنی

- **`monitor` مستقل از صف است:** سرپرستی تماس به `Job` وابسته نیست؛ به همین دلیل تماس ورودی وسط ارسال پیامک هم درست مدیریت می‌شود و صف نمی‌ایستد (`Modem` با `preempt_for_call` و call gate این را کامل می‌کند).
- **اعتماد مشروط به `+CLCC`:** فهرست خالی در همان ابتدای تماس قابل‌اعتماد نیست (تأخیر گزارش‌دهی مودم)؛ منطق `trusted` با نگه‌داشتن `clcc_seen_before_` و آستانه `2 × clcc_poll_ms` جلوی قطع کاذب را می‌گیرد.
- **`GSM_CALL_CLCC_MISSES = 2`:** دو پاسخ خالی پیاپی لازم است تا تماس «مرده» اعلام شود؛ حساسیت کمتر = مثبت کاذب بیشتر.
- **`ATH` fallback فقط یک بار:** پرچم `fallback_` تضمین می‌کند حلقه بی‌نهایت hangup رخ ندهد؛ بعد از `ATH` هر نتیجه‌ای پذیرفته و تماس بسته می‌شود.
- **`end_reason_` چسبنده است:** دلیل ثبت‌شده در تایمرها/تور ایمنی (`Timeout`، `RemoteHangup`) در `end_call` نهایی حفظ می‌شود و با `LocalHangup` بازنویسی نمی‌شود؛ گزارش `CallEndReason` به همین دلیل دقیق است.
- **زمان‌سنجی بدون heap:** همه تایمرها `uint32_t` مبتنی بر `at_.now()` و `hal::elapsed` (مقاوم به سرریز میلی‌ثانیه) هستند.
- **`duration_ms(now)`** فقط در `Active` معنا دارد و از `active_since_ms_` محاسبه می‌شود.
- **`engaged()`** (هر وضعیت غیر `Idle`) معیار «گیت تماس» در `Modem::pump_services` است: jobهای غیر-تماس تا پایان تماس نگه‌داشته می‌شوند اما صف از بین نمی‌رود.

## ارتباط با سایر فایل‌ها

- `gsm/voice/call_service.hpp` — تعریف `CallState`، `CallStep`، `CallEndReason`، `CallPolicy` و اعضای کلاس.
- `gsm/core/at_engine.hpp` — صدور فرمان‌ها، `abort()` و `now()`.
- `gsm/device/device_traits.hpp` — `hangup_cmd`، `supports_clcc`، `clcc_authoritative` (شخصی‌سازی هر مودم).
- `gsm/codec/phone_number.hpp` + `src/phone_number.cpp` — نرمال‌سازی شماره به E.164.
- `gsm/codec/ucs2.hpp` + `src/ucs2.cpp` — رمزگشایی شماره UCS2 در پاسخ‌های `+CLIP`/`+CLCC`.
- `src/modem.cpp` — `preempt_for_call`، call gate، `handle_urc` (Ring/CallerId/CallEnded/Busy) و `call_.monitor/pump` در `tick`.
- `src/cli.cpp` — نمایش وضعیت تماس، `callto <ms>` برای تنظیم `CallPolicy` و رویدادهای `[CALL]` / `[CALL-END]`.
- `include/gsm/gsm_config.hpp` — ثابت‌های `GSM_CALL_ANSWER_TIMEOUT_MS`، `GSM_CALL_RING_TIMEOUT_MS`، `GSM_CALL_CLCC_POLL_MS`، `GSM_CALL_CLCC_MISSES`.
