# gsm/include/gsm/maintenance/watchdog.hpp

## نقش فایل

نگهبان تعمیراتی ماژول GSM با **دو بازنشان مستقل** (resetter): یکی بر مبنای زمان (هر چند وقت یک‌بار ریست کامل دوره‌ای) و دیگری بر مبنای بودجهٔ عملیات (هر N عملیات موفق). بعد از هر بازنشانی یک **بررسی سلامت** (AT / CPIN / CREG / CSQ) صحت بازگشت ماژول را تأیید می‌کند. این سازوکار برای کاربردهای صنعتی طراحی شده که مودم باید ماه‌ها بدون دخالت انسان کار کند.

## ساختارهای کلیدی

### enumها
```cpp
enum class ResetReason : uint8_t
{
    None = 0,
    Periodic,      // resetter #1: time budget expired
    OpsBudget,     // resetter #2: operation budget expired
    Manual,
    FaultRecovery
};

enum class HealthState : uint8_t
{
    Unknown = 0, Checking, Healthy, Unhealthy
};

const char* to_string(ResetReason r);
const char* to_string(HealthState s);
```

### `struct HealthReport`
```cpp
struct HealthReport
{
    HealthState state          = HealthState::Unknown;
    bool        at_ok          = false;   // AT responsive
    bool        sim_ok         = false;   // +CPIN ready
    bool        net_ok         = false;   // +CREG registered
    int16_t     rssi_dbm       = 0;       // from live ModemStatus
    uint32_t    resets         = 0;       // total resets so far
    uint32_t    since_reset_ms = 0;
    bool        ok() const { return state == HealthState::Healthy; }
};
```

### `struct ResetPolicy` — قابل تنظیم در کامپایل و زمان اجرا (ویژگی v0.4)
```cpp
// Two independent reset budgets (both configurable at compile and run time):
//   #1 periodic_ms : full module reset every N ms (default 1 hour)
//   #2 ops_period  : full module reset after N completed operations
// After every reset a health check (AT / CPIN / CREG / CSQ) verifies the module.
struct ResetPolicy
{
    bool     enabled            = true;
    uint32_t periodic_ms        = GSM_PERIODIC_RESET_MS;   // 3600000, 0 = off
    uint16_t ops_period         = GSM_OPS_RESET_COUNT;     // 50, 0 = off
    uint32_t grace_ms           = GSM_WATCHDOG_GRACE_MS;   // 10000
    bool     use_power_cycle    = true;   // prefer power cycle over soft reset
    bool     health_check       = true;   // verify after reset
};
```

### `class MaintenanceWatchdog final : public IAtObserver`
```cpp
using ReportCb = Delegate<const HealthReport&>;

// --- budgets ---
void       note_operation();          // one finished operation (job)
void       tick(uint32_t now_ms);     // evaluate both budgets
bool       reset_pending() const;
ResetReason take_pending();           // consume the pending reset request
void       request_reset(ResetReason r = ResetReason::Manual);
void       on_reset_started(uint32_t now_ms);

// --- health check ---
void                bind_status(const ModemStatus* st);  // live SIM/network state (URC maintained)
void                start_check();
void                cancel_check();   // abandon an in-flight health check
bool                check_busy() const;
void                pump(AtEngine& at, bool engine_idle, bool modem_ready);
const HealthReport& health() const;

void set_on_health(ReportCb cb);

// IAtObserver: parse check responses
void on_line(const char* line, uint16_t len) override;
void on_final(const AtResult& res) override;

ResetPolicy&       policy();
const ResetPolicy& policy() const;
uint32_t           ops_count() const;
uint32_t           resets_done() const;
uint32_t           since_reset_ms(uint32_t now) const;
```

فیلدهای خصوصی: `last_reset_ms_`، `ops_`، `resets_`، `started_`، `pending_` (درخواست بازنشانی معلق)، `probe_index_`/`issued_`/`check_pending_` (ماشین حالت بررسی سلامت)، `status_` (اشاره‌گر به `ModemStatus` زنده)، `health_`، `on_health_`.

## جریان کاری

```text
Modem::tick()
    |
    +-> watchdog.note_operation()          // after each finished Job
    +-> watchdog.tick(now)                 // evaluate the two budgets
    |        |
    |        +-- now - last_reset >= periodic_ms   -> pending_ = Periodic
    |        +-- ops_ >= ops_period                -> pending_ = OpsBudget
    v
reset_pending()? -> take_pending() -> Modem::full_restart(hardware, keep_jobs)
    |                                        (power cycle if use_power_cycle)
    +-> on_reset_started(now)                // restart both budgets
    v
health check (if policy.health_check):
    start_check() -> probe sequence: AT -> +CPIN? -> +CREG? -> +CSQ
    |        (rssi from bound ModemStatus; probe_index_ walks the steps)
    v
finish_check() -> HealthReport{Healthy/Unhealthy} -> on_health(report)
    |                        |
    ok                       not ok -> FaultRecovery reset (via Modem)
```

نمونه:
```cpp
modem.maintenance().bind_status(&modem.status());
modem.maintenance().policy().ops_period = 100;    // reset every 100 jobs
modem.set_on_health([](const gsm::HealthReport& r) {
    if (!r.ok()) { /* alert / escalate */ }
});
```

## نکات فنی

- **بدون heap / بدون STL**: فقط PODها، `Delegate` و `StaticString`؛ مناسب اجرای ماه‌ها بدون پراکندگی حافظه.
- **دو بازنشان مستقل**: `periodic_ms` (پیش‌فرض ۱ ساعت) و `ops_period` (پیش‌فرض ۵۰ عملیات)؛ مقدار ۰ هر کدام را خاموش می‌کند. هر دو بعد از بازنشانی صفر می‌شوند (`on_reset_started`).
- **`grace_ms`** (پیش‌فرض ۱۰ ثانیه): اگر پنجرهٔ امن برای بازنشانی (موتور بیکار/مودم آماده) پیدا نشود، بازنشانی به‌زور اجرا می‌شود — جلوگیری از تعویق بی‌نهایت ریست دوره‌ای.
- `request_reset(Manual)` برای درخواست دستی از CLI/کاربر؛ `FaultRecovery` برای بازنشانی ناشی از شکست بررسی سلامت.
- `bind_status` اشاره‌گر به `ModemStatus` زنده است (مالکیتی ندارد)؛ `rssi_dbm` در گزارش از همین‌جا می‌آید.
- `pump(at, engine_idle, modem_ready)` فقط در پنجرهٔ امن اجرا می‌شود؛ `cancel_check` برای لغو بررسی نیمه‌کاره (مثلاً هنگام ورود تماس).
- `check_busy()` مانع هم‌پوشانی دو بررسی می‌شود؛ `probe_index_` ترتیب پروب‌ها را نگه می‌دارد.
- هماهنگی با `Modem`: `Modem::check_watchdog` و `Modem::maintenance_service` این APIها را اورکستریت می‌کنند؛ `set_on_health` در `Modem` هم در دسترس است.
- `IAtObserver` بودن کلاس یعنی پاسخ‌های پروب سلامت (مثلاً `+CPIN: READY`) مستقیم به `on_line`/`on_final` می‌آیند.

## ارتباط با سایر فایل‌ها

- `gsm_config.hpp` — `GSM_PERIODIC_RESET_MS`، `GSM_OPS_RESET_COUNT`، `GSM_WATCHDOG_GRACE_MS`
- `core/at_engine.hpp` — اجرای پروب‌های سلامت
- `core/at_types.hpp` — `IAtObserver`، `AtResult`
- `status/status_service.hpp` — `ModemStatus` (SIM/شبکه/RSSI زنده)
- `modem/modem.hpp` — اورکستراسیون `tick`/`full_restart`، `set_on_health`، `maintenance()`
- `util/delegate.hpp` — `ReportCb`
- `cli/cli.hpp` — `draw_watchdog()` و `on_health_thunk`
