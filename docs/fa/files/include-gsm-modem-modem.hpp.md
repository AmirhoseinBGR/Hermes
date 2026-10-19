# gsm/include/gsm/modem/modem.hpp

## نقش فایل

کلاس اصلی (Facade) کتابخانه: `Modem` مالک همهٔ زیرسامانه هاست — موتور AT، مسیریاب URC، سرویس های SMS/تماس/وضعیت/USSD، صف Jobها و نگهبان تعمیراتی — و چرخهٔ عمر کامل مودم را مدیریت می کند: بوت، ثبت شبکه، پالس PWRKEY، صف بندی درخواست های کاربر، housekeeping دوره ای، بازیابی SIM و ریست/پاورچرخه. کاربر نهایی معمولاً فقط با همین کلاس در تعامل است.

## ساختارهای کلیدی

### `enum class ModemState : uint8_t`
```cpp
enum class ModemState : uint8_t
{
    Off = 0,
    Booting,             // Sending AT probes
    PowerPulsing,        // Pulsing PWRKEY pin if no AT response
    SoftRebooting,       // AT+CFUN=1,1 software module reset
    WaitingRegistration, // Waiting for network registration (+CREG=1 or 5)
    Configuring,         // Applying bootstrap commands
    Ready,               // Idle, housekeeping active
    Working,             // Executing a job from queue
    Ringing,             // Incoming call
    InCall,              // Voice call active
    SimMissing,          // SIM card not inserted / removed
    Fault,               // Gave up (AT unresponsiveness or timeout)
};
```

### `struct HousekeepingConfig`
```cpp
struct HousekeepingConfig
{
    bool     enabled       = true;
    uint32_t signal_ms     = 15000;   // CSQ refresh
    uint32_t full_sweep_ms = 60000;   // full status sweep
    uint32_t sms_scan_ms   = 25000;   // unread scan
    uint32_t cleanup_ms    = 300000;  // purge_stale / storage cleanup
};
```
دسترسی: `Modem::housekeeping()`.

### `class Modem final : public IAtObserver`
سازنده:
```cpp
using SimEventCb = Delegate<SimState>;
Modem(hal::IUart& uart, hal::IClock& clock, const DeviceTraits& traits = kSim800,
      hal::IPowerControl* power = nullptr);
```

چرخهٔ عمر و تغذیه:
```cpp
void begin();        // start boot sequence
void tick();         // MUST be called in the main loop
void reset();        // soft restart of the library state machine
void power_cycle();  // full hardware power cycle (needs IPowerControl)
void soft_reboot();  // AT+CFUN=1,1 then boot guard + AT probe

bool has_power_control() const;
void pulse_pwrkey(uint32_t pulse_ms = GSM_PWRKEY_PULSE_MS);
bool pwrkey_asserted() const;  // پین همین حالا پایین نگه داشته شده است؟
uint32_t pwrkey_pulse_ms() const;
```

پولس PWRKEY **غیرمسدودکننده** است: `power_cycle()`/`pulse_pwrkey()` فقط وضعیت را به `PowerPulsing` می برند و `tick()` ترتیب
`assert` → نگه داشتن به مدت `pwrkey_pulse_ms` → `release` → انتظار `GSM_MODEM_BOOT_GUARD_MS` → پروب `AT` را اجرا می کند.
هیچ کدام از این توابع بلاک نمی کنند و هرگز نباید از داخل وقفه صدا زده شوند.
`NORMAL POWER DOWN` حین این دنباله به معنی خاموش شدن است (پولس بعدی برای روشن کردن)؛ بعد از `AT`/`RDY` موفق پیام کهنه است.

APIهای Job (همگی `JobId` برمی گردانند؛ callback اختیاری):
```cpp
JobId send_sms(const char* number, const char* utf8_text, JobCallback cb = {},
               SmsEncoding enc = SmsEncoding::Auto);
JobId read_sms(int16_t index, JobCallback cb = {});
JobId scan_sms(JobCallback cb = {});
JobId delete_read_sms(JobCallback cb = {});
JobId dial(const char* number, JobCallback cb = {});
JobId answer(JobCallback cb = {});
JobId hang_up(JobCallback cb = {});
JobId refresh_status(JobCallback cb = {});
JobId ussd(const char* code, JobCallback cb = {});
JobId check_balance(JobCallback cb = {});
```

کنترل صف:
```cpp
void       pause_queue();  void resume_queue();  bool queue_paused() const;
void       stop_all(bool also_pause = true);
bool       cancel(JobId id);
uint8_t    queue_size() const;
const Job* queued(uint8_t i) const;
const Job& current_job() const;
```

مشاهده و دسترسی به زیرسامانه ها:
```cpp
ModemState          state() const;
const char*         state_name() const;
const char*         activity() const;   // to_string(current_.kind)
const char*         step() const;
bool                ready() const;      // state == Ready
const ModemStatus&  status() const;
SmsService&         sms();
CallService&        call();
StatusService&      stat();
UssdService&        ussd_service();
AtEngine&           at();
HousekeepingConfig& housekeeping();
MaintenanceWatchdog& maintenance();
CallPolicy&         call_policy();
bool                call_gate() const;  // true while a call is engaged
```

ثبت رویداد:
```cpp
void set_on_sms(SmsService::Received cb);
void set_on_call(CallService::StateChanged cb);
void set_on_call_ended(CallService::Ended cb);
void set_on_health(MaintenanceWatchdog::ReportCb cb);
void set_on_job(JobCallback cb);
void set_on_urc(UrcRouter::Handler cb);
void set_on_sim(SimEventCb cb);
void set_auto_read_sms(bool on);   // default true
```

متدهای خصوصی کلیدی: `handle_urc`/`urc_trampoline`، `enter`، `enqueue`/`start_job`/`finish_job`، `pump_services`/`pump_housekeeping`/`flush_cancelled`، `service_for`، `check_watchdog`/`handle_sim_recovery`، `is_call_job`/`preempt_for_call`/`maintenance_service`/`full_restart` و وضعیت های بازیابی (`consecutive_timeouts_`، `sim_recovery_phase_`: 0=پروب `AT+CPIN?`، 1=‏`CFUN=0`، 2=‏`CFUN=1`).

## جریان کاری

```text
begin() -> Booting (AT probes) -> PowerPulsing (if silent) -> Configuring
        -> WaitingRegistration (+CREG 1/5) -> Ready
                                                      |
tick():  <--------------------------------------------+
  |-> at_.tick()                 (UART pump, URC routing)
  |-> call_.monitor(now, idle)   (supervision, auto-hangup, +CLCC)   [v0.4]
  |-> watchdog_.tick/pump        (dual resetters + health check)      [v0.4]
  |-> pump_services()            (drive current job state machine)
  |-> pop next Job if ready      (queue, call_gate holds non-call jobs)
  |-> pump_housekeeping()        (signal / sweep / scan / cleanup)
  |-> handle_sim_recovery()      (CPIN probe -> CFUN cycle -> power cycle)
  +-> flush_cancelled()          (cancelled job callbacks)

Job flow:  send_sms() -> enqueue() -> [queue] -> start_job() -> service_for()
              -> SmsService steps -> finish_job() -> JobCallback(job)
```

پیکربندی نگهبان تعمیراتی:
```cpp
modem.maintenance().policy().periodic_ms = 3600000;  // resetter #1
modem.maintenance().policy().ops_period = 50;        // resetter #2
modem.set_on_health(on_health);
```

## نکات فنی

- **بدون heap / بدون STL**: همهٔ سرویس ها به صورت member (نه new)؛ `current_` یک `Job` کامل روی object است.
- **`call_gate()`** (ویژگی v0.4): وقتی تماسی در جریان است (`CallService::engaged`)، Jobهای غیرتماسی نگه داشته می شوند تا وقفهٔ تماس، صف را خراب یا متوقف نکند؛ `preempt_for_call` اولویت را به Jobهای تماس می دهد.
- **بازیابی صنعتی**: timeoutهای پیاپی (`GSM_CONSECUTIVE_TIMEOUTS_RESET`) به بازیابی می انجامد؛ چرخهٔ SIM: پروب `AT+CPIN?` → `CFUN=0/1` → در نهایت power cycle سخت افزاری (پس از `GSM_HARDWARE_REBOOT_TIMEOUT_MS`).
- **`maintenance_service`/`full_restart`**: بازنشانی های نگهبان دوگانه (دوره ای + بودجهٔ عملیات) را اجرا می کنند؛ `keep_jobs` مشخص می کند صف حفظ یا تخلیه شود.
- `start_failures_` و `kErrJobStalled` مکانیزم جلوگیری از گیرافتادن صف روی Job غیرقابل شروع است.
- `tick()` باید بدون وقفه و سریع صدا زده شود؛ همه چیز غیرمسدودکننده و event-driven است.
- trait پیش فرض `kSim800` است؛ برای ماژول های M08/M08R صریحاً trait مناسب را بدهید (`kQuectelM08R` برای رفتار `clcc_authoritative`).
- `IPowerControl*` اختیاری است؛ بدون آن `reset()` نرم می ماند و `power_cycle()` معادل ریست نرم است.

## ارتباط با سایر فایل ها

- `core/at_engine.hpp`، `core/service.hpp`، `core/urc_router.hpp` — هستهٔ ارتباط AT
- `sms/sms_service.hpp`، `voice/call_service.hpp`، `status/status_service.hpp`، `ussd/ussd_service.hpp` — سرویس ها
- `queue/job.hpp`، `queue/job_queue.hpp` — صف درخواست ها
- `maintenance/watchdog.hpp` — نگهبان تعمیراتی و health check
- `device/device_traits.hpp` — رفتار ماژول محور
- `hal/hal.hpp` — UART/Clock/Power
- `cli/cli.hpp`، `tools/serial_bridge.hpp` — مصرف کنندگان سطح بالا
