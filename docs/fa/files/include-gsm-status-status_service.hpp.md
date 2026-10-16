# gsm/include/gsm/status/status_service.hpp

## نقش فایل

سرویس جمع‌آوری وضعیت مودم: سیگنال، ثبت شبکه، وضعیت سیم‌کارت، فضای ذخیرهٔ پیامک و اپراتور. نتایج در ساختار اشتراکی `ModemStatus` نوشته می‌شوند که هم `Modem` و هم CLI/کاربر از آن می‌خوانند. enumهای این فایل وضعیت شبکه/SIM/اپراتور را (از جمله اپراتورهای ایرانی) مدل می‌کنند.

## ساختارهای کلیدی

### enumهای وضعیت
```cpp
enum class RegState : uint8_t
{
    NotRegistered = 0, Home = 1, Searching = 2,
    Denied = 3, Unknown = 4, Roaming = 5
};

enum class SimState : uint8_t
{
    Unknown = 0, Ready, Removed, LockedPin, LockedPuk, Error
};

enum class Operator : uint8_t
{
    Unknown = 0,
    Irancell, // MTN Irancell
    Mci,      // Hamrah-e Aval / MCI / IR-TCI
    Rightel   // RighTel
};

const char* to_string(RegState s);
const char* to_string(SimState s);
const char* to_string(Operator op);
Operator    detect_operator(const char* name);
```
`detect_operator` نام اپراتور خوانده‌شده از `+COPS`/`+QSPN` را به enum نگاشت می‌کند (کاربرد اصلی: انتخاب خودکار کد تعادل USSD).

### `struct ModemStatus`
```cpp
struct ModemStatus
{
    uint8_t          csq_raw       = 99;   // AT+CSQ raw (99 = unknown)
    int8_t           rssi_dbm      = 0;
    uint8_t          ber           = 99;
    RegState         registration  = RegState::Unknown;
    SimState         sim_state     = SimState::Unknown;
    bool             sim_ready     = false;
    bool             network_ready = false;
    uint8_t          sms_used      = 0;
    uint8_t          sms_total     = 0;
    Operator         op            = Operator::Unknown;
    StaticString<32> operator_name;
    uint32_t         updated_at_ms = 0;

    uint8_t signal_bars() const;          // 0..5 mapped from csq_raw
    bool    storage_full() const;         // sms_total && sms_used >= sms_total
};
```
نگاشت `signal_bars`: ‏`csq_raw >= 20 → 5`، `>= 15 → 4`، `>= 11 → 3`، `>= 7 → 2`، غیر از ۹۹ → ۱، و ۹۹ → ۰.

### `enum class StatusStep : uint8_t`
```cpp
enum class StatusStep : uint8_t
{
    Idle = 0,
    Signal,        // AT+CSQ
    Registration,  // AT+CREG?
    SimPin,        // AT+CPIN?
    Storage,       // AT+CPMS? / storage_cmd
    Operator,      // AT+COPS?
    Done
};
```

### `class StatusService final : public IService`
```cpp
StatusService(AtEngine& engine, ModemStatus& status, const DeviceTraits& traits);

bool start(StatusStep first = StatusStep::Signal);  // from a given step
bool start_full_sweep();                            // all steps (JobKind::StatusSweep)

// IService overrides: active / pump / cancel / step_name / result
void on_line(const char* line, uint16_t len) override;
void on_final(const AtResult& res) override;
```
فیلد `sweep_` مشخص می‌کند اجرا کامل است یا تک‌مرحله‌ای.

## جریان کاری

```text
Modem housekeeping (HousekeepingConfig.signal_ms / full_sweep_ms)
    |
    v
StatusService::start_full_sweep()
    |
    +-> Signal        : parse +CSQ <rssi>,<ber>  -> csq_raw / rssi_dbm / ber
    +-> Registration  : parse +CREG:<n>,<stat>   -> registration
    +-> SimPin        : parse +CPIN: READY/...   -> sim_state / sim_ready
    +-> Storage       : parse +CPMS used,total   -> sms_used / sms_total
    +-> Operator      : parse +COPS:<mode>,<format>,"name" -> operator_name / op
    v
updated_at_ms = now  -> ModemStatus ready for CLI/status bar / watchdog health
```

## نکات فنی

- **بدون تخصیص حافظه**: `ModemStatus` یک POD با `StaticString<32>`؛ به‌صورت ارجاع در `Modem` نگهداری و درجا به‌روز می‌شود.
- `csq_raw = 99` یعنی «نامشخص» و نباید به dBm تبدیل شود؛ `signal_bars()` همین را به ۰ نگاشت می‌کند.
- مقادیر `RegState` عمداً با کدهای `+CREG` یکی‌اند (0..5) تا نگاشت بدون تبدیل باشد.
- `detect_operator` برای اپراتورهای ایرانی بهینه شده؛ نام‌های ناشناخته به `Operator::Unknown` می‌رسند.
- نتایج این سرویس ورودی `MaintenanceWatchdog::bind_status` هستند (بررسی سلامت SIM/شبکه).
- `start()` با `first` دلخواه اجازه می‌دهد فقط بخشی (مثلاً فقط سیگنال) تازه‌سازی شود — بهینه‌سازی housekeeping دوره‌ای.

## ارتباط با سایر فایل‌ها

- `core/service.hpp` — پایهٔ `IService`
- `core/at_engine.hpp` — اجرای `AT+CSQ/CREG/CPIN/CPMS/COPS`
- `device/device_traits.hpp` — `storage_cmd` برای انتخاب مسیر ذخیره
- `modem/modem.hpp` — مالک `ModemStatus status_` و زمان‌بند housekeeping
- `maintenance/watchdog.hpp` — `bind_status(const ModemStatus*)` برای health check
- `ussd/ussd_service.hpp` — `Operator` برای `start_balance_query`
- `cli/cli.hpp` — نمایش status bar و signal bars
