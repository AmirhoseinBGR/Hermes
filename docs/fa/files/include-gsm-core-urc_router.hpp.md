# gsm/include/gsm/core/urc_router.hpp

## نقش فایل

مسیریابی پیام‌های ناخواستهٔ مودم (URC — Unsolicited Result Code) مثل `RING`، `+CMTI`، `+CREG`. این کلاس خطوطی را که در پاسخ به فرمان خاصی نیامده‌اند طبقه‌بندی می‌کند و به‌صورت رویداد ساخت‌یافته (`UrcEvent`) به یک handler واحد تحویل می‌دهد. این جداسازی باعث می‌شود پروتکل‌های ناخواسته، جریان تراکنش‌های AT را مختل نکنند.

## ساختارهای کلیدی

### `enum class Urc : uint8_t`
انواع رویداد شناخته‌شده:
```cpp
enum class Urc : uint8_t
{
    Unknown = 0,
    Ring,            // RING — incoming call
    CallerId,        // +CLIP: "number",...
    CallEnded,       // NO CARRIER / NO ANSWER
    CallBusy,        // BUSY
    NoAnswer,        // NO ANSWER
    SmsReceived,     // +CMTI: "SM",index
    SmsDeliveryRep,  // +CDS: delivery report
    SimReady,        // +CPIN: READY
    SimRemoved,      // +CREG / SIM removal indication
    SimInserted,
    CallReady,       // CALL READY
    SmsReady,        // SMS READY
    PowerDown,       // NORMAL POWER DOWN
    NetworkReg,      // +CREG: <stat>
    SignalReport,    // +CSQ / +CIEV signal indications
    Ussd,            // +CUSD: <m>,"<str>",<dcs>
    ModemReady,
};
```

### `struct UrcEvent`
```cpp
struct UrcEvent
{
    Urc         kind = Urc::Unknown;
    const char* line = nullptr;  // the raw URC line
    int32_t     arg  = -1;       // parsed numeric payload (e.g. +CMTI index, +CREG stat)
};
```

### `class UrcRouter`
```cpp
class UrcRouter
{
  public:
    using Handler = Delegate<const UrcEvent&>;

    void       set_handler(Handler h) { handler_ = h; }
    bool       dispatch(const char* line, uint16_t len);   // true if it was a URC
    static Urc classify(const char* line, int32_t& arg);   // line -> Urc + parsed arg

  private:
    Handler handler_;
};
```
- `classify`: تشخیص نوع URC از پیشوند/الگوی خط (`RING`، `+CMTI:`، `+CLIP:`، `+CUSD:`، …) و استخراج آرگومان عددی.
- `dispatch`: اگر خط URC باشد، handler را صدا می‌زند و `true` برمی‌گرداند؛ در غیر این صورت `false` (خط، پاسخ فرمان جاری است).

## جریان کاری

```text
AtEngine::tick()
    |
    | line complete
    v
UrcRouter.dispatch(line, len)
    |            \
    | true        \ false
    v              v
Modem::handle_urc()   -> service observer (on_line / on_final)
    |
    +-> Ring / CallerId     -> CallService::notify_ring()
    +-> CallEnded / Busy    -> CallService::notify_hangup()/notify_busy()
    +-> SmsReceived         -> auto read (SmsService) if enabled
    +-> SimReady/Removed    -> Modem::handle_sim_recovery(), on_sim callback
    +-> NetworkReg          -> registration state machine
    +-> Ussd                -> UssdService::notify_cusd()
    +-> user_urc_ handler   -> application-level URC hook
```

نمونهٔ ثبت handler (در `Modem` با trampoline):
```cpp
// Static trampoline since Delegate stores a plain function + context.
urc_.set_handler(UrcRouter::Handler{&Modem::urc_trampoline, this});
```

## نکات فنی

- **بدون تخصیص حافظه**: `UrcEvent` فقط اشاره‌گر به خط موجود و یک `int32_t`؛ متن خط متعلق به بافر `LineReader` است و فقط تا پردازش بعدی معتبر است.
- `Delegate` به‌جای `std::function` استفاده شده (بدون heap و بدون STL).
- تشخیص URC در برابر پاسخ فرمان بر عهدهٔ `classify` (الگوی خط) و `AtEngine` است؛ خطوطی که الگوی URC داشته باشند از جریان پاسخ تراکنش جدا می‌شوند.
- URCهای ناشناخته به `Urc::Unknown` طبقه‌بندی می‌شوند و از طریق `user_urc_` در دسترس کاربرند (هدر ناشناس دور انداخته نمی‌شود).
- برای ماژول‌هایی مثل M08R که URCهای پایان تماس نامطمئن‌اند (`clcc_authoritative`)، `CallEnded`/`CallBusy` صرفاً مکمل‌اند و نباید تنها مرجع قطع تماس باشند (به `voice/call_service.hpp` نگاه کنید).

## ارتباط با سایر فایل‌ها

- `core/at_engine.hpp` — فراخوانی `dispatch` برای هر خط دریافتی
- `util/delegate.hpp` — نوع `Handler`
- `modem/modem.hpp` — handler اصلی (`handle_urc` + `urc_trampoline`) و `set_on_urc`
- `voice/call_service.hpp` — `notify_ring` / `notify_hangup` / `notify_busy`
- `ussd/ussd_service.hpp` — `notify_cusd` برای `Urc::Ussd`
- `status/status_service.hpp` — به‌روزرسانی وضعیت شبکه/SIM از رویدادها
