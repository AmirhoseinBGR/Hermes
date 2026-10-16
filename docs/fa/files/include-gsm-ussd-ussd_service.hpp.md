# gsm/include/gsm/ussd/ussd_service.hpp

## نقش فایل

سرویس USSD: اجرای کدهای دستوری مثل `#123#` (موجودی، بسته، منوی اپراتور) و مدیریت **نشست تعاملی** — وقتی شبکه برای ورودی بیشتر درخواست می‌دهد، سرویس در حالت `SessionOpen` می‌ماند تا کاربر پاسخ بفرستد (`send_reply`) یا نشست را ببندد (`end_session`). امکان پرس‌وجوی خودکار موجودی بر اساس اپراتور شناسایی‌شده هم وجود دارد.

## ساختارهای کلیدی

### `struct UssdResult`
```cpp
struct UssdResult
{
    StaticString<GSM_USSD_RESPONSE_BYTES> response;
    AtResult                              result;
    int16_t status_code = -1; // 0=no further action, 1=further action needed
};
```
`status_code` همان `m` در `+CUSD` است: ۰ = پایان تعامل، ۱ = ادامهٔ تعامل.

### `enum class UssdStep : uint8_t`
```cpp
enum class UssdStep : uint8_t
{
    Idle = 0,
    SetUssdMode,   // AT+CUSD=1
    SendQuery,     // AT+CUSD=1,"<code>",15
    SendReply,     // sending a reply inside an already-open interactive session
    SessionOpen,   // network asked for more input (CUSD m==1), waiting for send_reply()/end_session()
    EndSession,    // sending AT+CUSD=2 to close the session
    WaitUrc,       // waiting for the +CUSD response
    Done
};
```

### `class UssdService final : public IService`
```cpp
using UssdDone = Delegate<const UssdResult&>;

UssdService(AtEngine& engine, SessionState& session, const DeviceTraits& traits);

bool start_query(const char* ussd_code);
bool start_balance_query(Operator op);   // auto code by detected operator

// IService overrides: active / pump / cancel / step_name / result
void on_line(const char* line, uint16_t len) override;
void on_final(const AtResult& res) override;

// Handles +CUSD: <m>,"<str>",<dcs>
void notify_cusd(const char* line);

const UssdResult& last_result() const;
void              set_on_complete(UssdDone cb);

static const char* default_balance_code(Operator op);

// interactive session control
bool send_reply(const char* text);
bool end_session();
bool session_open() const;
```

متدهای خصوصی: `goto_step`، `complete(AtStatus, int16_t)` و فیلد `wait_start_` برای timeout انتظار URC.

## جریان کاری

```text
start_query("*140#")  یا  start_balance_query(op)
    |
    v
SetUssdMode -> SendQuery (AT+CUSD=1,"*140#",15)
    |
    v
WaitUrc  <-----+  (+CUSD arrives via UrcRouter -> Modem -> notify_cusd)
    |          |
    |  m==0: response captured -> Done -> on_complete(UssdResult)
    |  m==1: SessionOpen (interactive menu)
    |            |
    |            +-> send_reply("1") -> SendReply -> WaitUrc   (loop)
    |            +-> end_session()   -> EndSession (AT+CUSD=2) -> Done
    v
timeout (GSM_AT_USSD_TIMEOUT) -> complete(Timeout)
```

نمونه:
```cpp
modem.ussd_service().set_on_complete(cb);
modem.check_balance();   // Job wrapper -> start_balance_query(status.op)
```

## نکات فنی

- **بدون تخصیص حافظه**: پاسخ در `StaticString<GSM_USSD_RESPONSE_BYTES>` (پیش‌فرض ۵۱۲ بایت)؛ کد USSD در `StaticString<32>`.
- پاسخ USSD معمولاً با DCS آمده و ممکن است UCS2 باشد؛ رمزگشایی متن بر عهدهٔ لایهٔ `codec` و مصرف‌کننده است — `response` همان رشتهٔ خام داخل `+CUSD` است.
- `notify_cusd` مستقل از جریان `pump` است: URC ممکن است در هر لحظه (حتی بیرون از WaitUrc) برسد.
- حالت `SessionOpen` یعنی نشست شبکه باز است؛ بی‌توجهی به آن باعث قفل‌شدن نشست USSD اپراتور می‌شود — یا `send_reply` یا `end_session` اجباری است.
- timeout پیش‌فرض `GSM_AT_USSD_TIMEOUT` (۱۰ ثانیه)؛ برخی اپراتورها کندند — در صورت نیاز `wait_start_`/timeout را در سطح Job تنظیم کنید.
- `default_balance_code(Operator)` برای Irancell/MCI/Rightel کدهای استاندارد تعادل را برمی‌گرداند (وابسته به `detect_operator` در `status_service.hpp`).

## ارتباط با سایر فایل‌ها

- `core/service.hpp` — پایهٔ `IService` و `SessionState`
- `core/at_engine.hpp` — اجرای `AT+CUSD`
- `core/urc_router.hpp` — مسیر `Urc::Ussd` به `notify_cusd`
- `status/status_service.hpp` — enum `Operator` و `detect_operator`
- `queue/job.hpp` — `JobKind::UssdQuery` و بافر `ussd_response`
- `modem/modem.hpp` — Jobهای `ussd()` و `check_balance()`
- `gsm_config.hpp` — `GSM_SMS_NUMBER_BYTES`/`GSM_USSD_RESPONSE_BYTES`/`GSM_AT_USSD_TIMEOUT`
