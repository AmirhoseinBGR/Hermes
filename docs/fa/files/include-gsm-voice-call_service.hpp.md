# gsm/include/gsm/voice/call_service.hpp

## نقش فایل

سرویس مدیریت تماس صوتی: شماره‌گیری، پاسخ، قطع تماس و مهم‌تر از همه **نظارت خودران بر تماس (supervision)** در نسخهٔ v0.4 — تایمرهای قطع/رد خودکار، polling متناوب `+CLCC` به‌عنوان تور ایمنی (و مرجع اصلی در ماژول‌های M08R) و ثبت دلیل پایان تماس. هدف این است که تماس‌های نیمه‌کاره هرگز صف Jobها را قفل نکنند و مودم در وضعیت تماس معلق نماند.

## ساختارهای کلیدی

### enumهای وضعیت
```cpp
enum class CallState : uint8_t
{
    Idle = 0, Dialing, Outgoing, Incoming, Active, Releasing
};

// Why a call went back to Idle (last ended call only).
enum class CallEndReason : uint8_t
{
    None = 0, LocalHangup, RemoteHangup, Busy, NoAnswer,
    NoCarrier, NoDialtone, Timeout, Error
};

enum class CallStep : uint8_t
{
    Idle = 0, EnableClip, Dial, Answer, Hangup, HangupFallback, ClccProbe, Done
};

const char* to_string(CallState s);
const char* to_string(CallStep s);
const char* to_string(CallEndReason r);
```
`HangupFallback`: اگر دستور قطع اصلی (مثلاً `AT+CHUP`) جواب ندهد، تلاش با `ATH` (یا بالعکس، بسته به trait). `ClccProbe`: یک پروب `+CLCC` در جریان (مستقل از مراحل Job).

### `struct CallPolicy` — قواعد قابل تنظیم در زمان اجرا (ویژگی v0.4)
```cpp
struct CallPolicy
{
    // Auto-hangup an outgoing call that is not established within this window.
    uint32_t dial_timeout_ms = GSM_CALL_ANSWER_TIMEOUT_MS;   // 20s
    // Auto-reject an incoming call that is not answered within this window.
    uint32_t ring_timeout_ms = GSM_CALL_RING_TIMEOUT_MS;     // 20s
    // +CLCC call-state polling interval (also the safety net for missed URCs).
    uint32_t clcc_poll_ms = GSM_CALL_CLCC_POLL_MS;           // 2s
    // Master switch for the automatic hangup / reject timers.
    bool auto_hangup = true;
};
```
دسترسی: `CallService::policy()` و `Modem::call_policy()`.

### `class CallService final : public IService`
```cpp
using StateChanged = Delegate<CallState, const char*>;  // state + peer number
using Ended        = Delegate<CallEndReason>;

CallService(AtEngine& engine, SessionState& session, const DeviceTraits& traits);

bool start_dial(const char* number_e164);
bool start_answer();
bool start_hangup();

// Self-driving supervision: timers, auto-hangup and +CLCC polling. Called on
// every Modem tick even when no call job is the current job, so an incoming
// call that lands between SMS jobs can never wedge the queue.
void monitor(uint32_t now_ms, bool engine_idle);

// IService: active / pump / cancel / step_name / result + IAtObserver
void on_line(const char* line, uint16_t len) override;
void on_final(const AtResult& res) override;

// URC notifications (from Modem::handle_urc)
void notify_ring(const char* caller);
void notify_hangup();
void notify_busy();

// state queries
CallState     state() const;
const char*   peer() const;            // PhoneNumber::e164 of the peer
bool          in_call() const;         // state == Active
bool          ringing() const;         // state == Incoming
bool          engaged() const;         // state != Idle (job queue gate)
CallEndReason last_end_reason() const;
uint32_t      duration_ms(uint32_t now_ms) const;

// configuration
void set_on_state(StateChanged cb);
void set_on_ended(Ended cb);
void set_auto_clip(bool on);           // enable +CLIP for caller ID (default true)
CallPolicy&       policy();
const CallPolicy& policy() const;
```

فیلدهای نظارتی داخلی: `dial_since_ms_`، `ring_since_ms_`، `active_since_ms_`، `state_since_ms_`، `last_clcc_ms_`، `clcc_misses_`، `clcc_seen_`، `clcc_seen_before_` — تایمرها و شمارندهٔ پروب‌های `+CLCC`.

## جریان کاری

```text
                       Modem::tick() -> CallService::monitor(now, engine_idle)
                                       (even with no call job in the queue)
        |
        +-- Incoming:  RING/+CLIP -> notify_ring() -> state=Incoming, ring timer
        |      no answer in ring_timeout_ms  -> auto reject (Hangup)   [v0.4]
        |
        +-- Outgoing:  start_dial() -> EnableClip -> Dial -> Dialing/Outgoing
        |      not established in dial_timeout_ms -> auto-hangup
        |      (end_reason=Timeout, result.code=kErrCallTimeout)       [v0.4]
        |
        +-- Active:    +CLCC polling every clcc_poll_ms
        |      (clcc_authoritative on M08R: CLCC is the source of truth)
        |      clcc_misses_ >= GSM_CALL_CLCC_MISSES -> call declared dead
        |      NO CARRIER / BUSY / ATH -> end_call(reason) -> on_ended(reason)
        |
        +-- Hangup flow: Hangup (trait hangup_cmd) -> HangupFallback (ATH/CHUP)
```

نمونه:
```cpp
modem.set_on_call_ended([](gsm::CallEndReason r) { /* ... */ });
modem.call_policy().auto_hangup = true;
modem.dial("+98912xxxxxxx", cb);
```

## نکات فنی

- **بدون تخصیص حافظه**: `PhoneNumber peer_` از نوع `StaticString`؛ همهٔ تایمرها `uint32_t` با الگوی `hal::elapsed`.
- **`clcc_authoritative`** (از `DeviceTraits`): برای M08R اعتماد اصلی به `+CLCC` است نه URCهای `NO CARRIER`/`BUSY` — این URCها در این ماژول نامطمئن‌اند و ماژول ممکن است بعد از payload پیامکی قطع‌شده گیر کند.
- `monitor` عمداً **جدا از Job** کار می‌کند: تماس ورودی بین دو Job پیامک هم دیده می‌شود؛ `Modem::call_gate()` بر اساس `engaged()` جلوی Jobهای غیرتماسی را می‌گیرد تا صف خراب نشود.
- `clcc_misses_` با آستانهٔ `GSM_CALL_CLCC_MISSES` (پیش‌فرض ۲) مرگ تماس را اعلام می‌کند — پاسخ به URCهای از‌دست‌رفته.
- `fallback_` تضمین می‌کند برای هر قطع فقط یک بار fallback انجام شود؛ `HangupFallback` از حلقهٔ بی‌نهایت جلوگیری می‌کند.
- پایان با `kErrCallTimeout` (خطای سطح کتابخانه) به‌همراه `CallEndReason::Timeout` گزارش می‌شود.
- `CallPolicy` در زمان اجرا قابل تنظیم است؛ همهٔ مقادیر پیش‌فرض از `gsm_config.hpp` می‌آیند.
- `enable_clip` (خودکار با `set_auto_clip`) شمارهٔ تماس‌گیرنده را از `+CLIP` می‌گیرد؛ `peer()` فقط بعد از دریافت CLIP یا شماره‌گیری معتبر است.

## ارتباط با سایر فایل‌ها

- `core/service.hpp` — پایهٔ `IService`
- `core/at_engine.hpp` — اجرای `ATD`/`ATA`/`AT+CHUP`/`ATH`/`AT+CLCC`
- `core/urc_router.hpp` — `Ring`/`CallerId`/`CallEnded`/`CallBusy` به `notify_*`
- `device/device_traits.hpp` — `hangup_cmd`، `supports_clcc`، `clcc_authoritative`
- `codec/phone_number.hpp` — `PhoneNumber peer_`
- `gsm_config.hpp` — `GSM_CALL_*` (پیش‌فرض‌های `CallPolicy`)، `kErrCallTimeout` در `at_types.hpp`
- `modem/modem.hpp` — Jobهای `CallDial/CallAnswer/CallHangup`، `call_gate()` و `preempt_for_call`
- `cli/cli.hpp` — نمایش وضعیت تماس و `CallEndReason`
