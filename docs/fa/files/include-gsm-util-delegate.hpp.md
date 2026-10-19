# gsm/include/gsm/util/delegate.hpp

## نقش فایل

جایگزین سبک وزن و بدون heap برای `std::function`: یک «delegate» قالب بندی شده با امضای دلخواه که از یک **تابع + اشاره گر context** تشکیل شده است. همهٔ callbackهای کتابخانه (رویدادهای SMS/تماس/Job/URC/سلامت) از این نوع اند تا بدون RTTI، استثنا یا تخصیص حافظهٔ پویا کار کنند.

## ساختارهای کلیدی

### `template <typename... Args> class Delegate`
```cpp
template <typename... Args> class Delegate
{
  public:
    using Fn = void (*)(void* ctx, Args...);

    constexpr Delegate() = default;
    constexpr Delegate(Fn fn, void* ctx) : fn_(fn), ctx_(ctx) {}

    void operator()(Args... args) const;   // invoke (no-op if empty)
    explicit operator bool() const;        // bound?
    void     clear();                      // unbind

    template <typename T, void (T::*M)(Args...)> static Delegate bind(T* obj);

  private:
    Fn    fn_  = nullptr;
    void* ctx_ = nullptr;
};
```

- سازندهٔ صریح: `Delegate(fn, ctx)` — تابع ایستا با context دلخواه.
- `operator()`: فراخوانی؛ اگر bind نشده باشد بی صدا کاری نمی کند (فراخوانی امن روی delegate خالی).
- `operator bool`: بررسی bind بودن.
- **`bind<T, &T::method>(obj)`**: اتصال متد عضو یک object بدون lambda یا heap؛ از یک lambda بدون capture (تبدیل شونده به اشاره گر تابع) استفاده می کند که `ctx` را به `T*` cast می کند.

نمونه:
```cpp
// Free function + context (used by Modem's URC trampoline).
Delegate<const UrcEvent&> h{&Modem::urc_trampoline, this};

// Member function binding without std::function.
auto cb = Delegate<const Job&>::bind<App, &App::on_job_done>(&app);
```

## جریان کاری

```text
producer (AtEngine / Modem / services)
    |
    |  delegate(args...)  ==  fn_(ctx_, args...)
    v
consumer method on the bound object   (or plain function)
```

الگوی trampoline در `Modem`:
```cpp
// Static thunk forwards to the instance method via the stored context.
static void urc_trampoline(void* ctx, const UrcEvent& ev)
{ static_cast<Modem*>(ctx)->handle_urc(ev); }
```

## نکات فنی

- **بدون heap، بدون STL، بدون closure**: فقط یک اشاره گر تابع + یک `void*`؛ اندازهٔ object ثابت و کوچک (معمولاً ۸/۱۶ بایت).
- `constexpr` بودن سازنده ها اجازه می دهد delegateها در مقداردهی اولیهٔ سراسری ساخته شوند.
- ایمنی نوعی `bind<T, &T::M>` در زمان کامپایل بررسی می شود (قالب عضو با امضای دقیق)؛ خبری از `dynamic_cast` نیست.
- delegate خالی ایمن است: `operator()` بدون `fn_` هیچ کاری نمی کند — نیازی به بررسی قبل از فراخوانی نیست (هرچند `operator bool` موجود است).
- محدودیت: فقط اشاره گر ساده به object؛ هوشمندی مالکیت (shared_ptr و …) ندارد — طول عمر object بر عهدهٔ کاربر است.
- الگوی استاندارد کتابخانه: `using X = Delegate<Args...>;` مثل `JobCallback`، `UrcRouter::Handler`، `SmsService::Received`، `MaintenanceWatchdog::ReportCb`.

## ارتباط با سایر فایل ها

- `core/urc_router.hpp` — `UrcRouter::Handler`
- `queue/job.hpp` — `JobCallback`
- `sms/sms_service.hpp` — `SendDone` / `Received`
- `ussd/ussd_service.hpp` — `UssdDone`
- `voice/call_service.hpp` — `StateChanged` / `Ended` (با دو آرگومان)
- `maintenance/watchdog.hpp` — `ReportCb`
- `modem/modem.hpp` — `SimEventCb` و ثبت callbackهای کاربر
