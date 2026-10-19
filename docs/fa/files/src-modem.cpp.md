# gsm/src/modem.cpp

## نقش فایل

پیاده سازی `gsm::Modem`؛ هسته مرکزی کتابخانه که همه چیز را کنار هم می نشاند: ماشین وضعیت بوت/ثبت شبکه، صف jobها (`JobQueue`)، سرویس های SMS/تماس/وضعیت/USSD، مسیریاب URC، سگ نگهبان صنعتی و watchdog نگهداری. این فایل در v0.4 چهار قابلیت کلیدی گرفته است: **گیت تماس** (نگه داشتن jobهای غیر-تماسی بدون از بین رفتن صف)، **رفع گیرکردن `pump_services`** (jobهایی که start نمی شوند بعد از چند تلاش fail می شوند به جای قفل کردن صف)، **`preempt_for_call`** (لغو ESC-safe ارسال پیامک در جریان هنگام ورود تماس) و **`maintenance_service` / `full_restart`** (reset کامل زمان بندی شده که صف jobها را حفظ می کند).

## توابع و منطق کلیدی

### جدول بوت `kBoot`
ترتیب فرمان های راه اندازی بعد از ثبت شبکه:

```cpp
{"ATE0", 1000, false},      {"AT+CMEE=2", 1000, true},   // optional
{"AT+CLIP=1", 1000, true},  {"AT+CNMI=2,1,0,0,0", 1000, true},
{"AT+CMGF=1", 1000, false}, {"AT+CSCS=\"UCS2\"", 1000, false},
{nullptr, 0, false},   // → traits_.csmp_ucs2
{nullptr, 0, false},   // → traits_.storage_cmd
```

دو مورد `nullptr` در `tick` از `DeviceTraits` پر می شوند؛ اگر دستگاهی چنین فرمانی نداشته باشد، همان لحظه نتیجه موفق مصنوعی می گیرد و جلو می رود. فرمان های non-optional در صورت شکست FSM را به `Fault` می برند.

### `to_string(ModemState)`
`"off" | "booting" | "pwrkey-pulsing" | "waiting-reg" | "configuring" | "ready" | "working" | "ringing" | "in-call" | "sim-missing" | "fault"`.

### چرخه حیات: `begin()` / `reset()` / `power_cycle()` / `full_restart(...)`
- `begin()` — صفر کردن شمارنده های بازیابی، ثبت زمان در watchdog (`on_reset_started`)، ورود به `Booting`.
- `reset()` — `at_.abort()` + `stop_all(false)` + `begin()` (فقط FSM، ماژول ریست نمی شود).
- `soft_reboot()` — ریست نرم ماژول با `AT+CFUN=1,1`، سپس boot guard و پروب `AT`.
- `power_cycle()` — پالس PWRKEY در صورت وجود `IPowerControl`، سپس `reset()`.
- **`full_restart(bool hardware, bool keep_jobs)` (v0.4):** راه اندازی مجدد کامل با انتخاب سیاست صف:

```cpp
at_.abort();
if (keep_jobs) {
    // فقط job در حال اجرا لغو و نهایی می شود؛ صف انتظار دست نخورده می ماند
    if (current_.state == JobState::Running) { svc->cancel(); finish_job({Aborted,-1}); }
} else {
    stop_all(false);   // پاک سازی کامل صف
}
if (hardware && power_) power_->power_key(true), power_->power_key(false);
begin();
```

### گیت تماس (v0.4)
```cpp
bool Modem::is_call_job(JobKind kind);   // CallDial | CallAnswer | CallHangup
bool Modem::call_gate() const            // call_.state() != CallState::Idle
```
هر job غیر-تماسی وقتی تماسی در جریان است (`engaged`) **نگه داشته** می شود — نه لغو، نه حذف؛ صف بعد از پایان تماس ادامه می یابد.

### `void Modem::preempt_for_call()` (v0.4)
هنگام ورود تماس (URC `RING`/`+CLIP`) روی job در حال اجرا صدا زده می شود:

```cpp
if (current_.state != JobState::Running || is_call_job(current_.kind)) return;
IService* svc = service_for(current_.kind);
if (svc) svc->cancel();              // ESC-safe abort of any pending SMS payload
current_.deferred = true;
current_.state    = JobState::Queued; // بازگشت به صف برای اجرای مجدد بعد از تماس
```

به لطف `SmsService::cancel` → `at_.abort()` → `write_escape()`، مودمی که در پرامپت `CMGS` گیر کرده بدون خراب شدن payload از حالت داده خارج می شود. پرچم `deferred` در `start_job` باعث می شود بودجه تلاش (`attempts`) برای این اجرای مجدد مصرف نشود.

### `void Modem::maintenance_service()` (v0.4)
پل بین `MaintenanceWatchdog` و اجرای واقعی reset:

```cpp
watchdog_.tick(clock_.millis());
// پنجره امن: بدون تماس، بدون فرمان AT در پرواز، بدون job جاری
const bool safe = call_.state() == CallState::Idle && at_.idle() && current_.kind == JobKind::None;
const bool forced = hal::elapsed(now, maintenance_since_, watchdog_.policy().grace_ms);
if (!safe && !forced) return;
full_restart(hardware_power_cycle, /*keep_jobs*/ true);
```

اگر پنجره امن پیدا نشود، حداکثر به اندازه `grace_ms` (پیش فرض ۱۰s) صبر می کند و بعد reset «اجباری» انجام می شود. در هر دو حالت صف jobها حفظ می شود.

### صف بندی: `enqueue` و متدهای عمومی
`enqueue(Job&, JobCallback)` شماره یکتای `JobId` (از ۱، بدون صفر)، زمان ورود و وضعیت `Queued` ثبت می کند؛ صف پُر → `kInvalidJob`. متدهای راحتی: `send_sms` (با **محافظ طول** — پایین تر)، `read_sms`، `scan_sms`، `delete_read_sms`، `dial`، `answer` (urgent)، `hang_up` (urgent)، `refresh_status`، `ussd`، `check_balance` (کد پیش فرض اپراتور ایرانی از `UssdService::default_balance_code`).

### `JobId Modem::send_sms(...)` — محافظ سخت گیرانه طول (v0.4)
قبل از ورود به صف، متن با `SmsService::count_chars` شمرده و با `sms_.max_chars()` (پیش فرض `GSM_SMS_MAX_CHARS = 60`) مقایسه می شود؛ در صورت تجاوز، job بلافاصله `Failed` با `kErrSmsTooLong` به callback گزارش و `kInvalidJob` برمی گردد (بدون اشغال صف):

```cpp
if (limit && chars > limit) {
    job.state  = JobState::Failed;
    job.result = AtResult{AtStatus::Error, kErrSmsTooLong};
    cb(job); on_job_(job);
    return kInvalidJob;
}
```

### `bool Modem::start_job(Job&)`
اعلام شروع به سرویس متناظر (`service_for`)؛ برای job برگشتی از `preempt_for_call` بودجه تلاش مصرف نمی شود (`deferred`)، وگرنه `attempts++`. در موفقیت: `JobState::Running` + `enter(Working)`.

### `void Modem::finish_job(const AtResult&)` — سیاست تلاش مجدد
- `SmsSend` شماره مرجع (`reference`) و `UssdQuery` پاسخ (`ussd_response`) را در job کپی می کند.
- timeout پیاپی شمرده می شود (فقط جاب، نه پروب بوت)؛ از `GSM_CONSECUTIVE_TIMEOUTS_RESET` (۳) بیشتر → در تیک بعدی `check_watchdog()` با `AT+CFUN=1,1` بازیابی می کند. `finish_job` دیگر `check_watchdog` را صدا نمی زند و وسط CFUN/PWRKEY به `Ready` برنمی گردد.
- **سیاست های رد (`kErrSmsTooLong` / `kErrJobStalled`) و لغو (`Aborted`) هرگز retry نمی شوند.**
- خطای retry پذیر تا `max_attempts` (برای SMS دو) دوباره به صف برمی گردد (`JobState::Queued`).
- پس از نهایی شدن: `watchdog_.note_operation()` (تغذیه بودجه عملیات watchdog)، وضعیت نهایی `Succeeded/Cancelled/Failed`، فراخوانی callback و `on_job_`، و تعیین وضعیت FSM (`InCall`/`Ringing`/`Ready`).

### `void Modem::handle_urc(const UrcEvent&)`
مرکز فرمان دهی رویدادهای ناخواسته:

| رویداد | اقدام |
|---|---|
| `Ring` / `CallerId` | `preempt_for_call()` + `call_.notify_ring(...)` + `enter(Ringing)` |
| `CallEnded` / `NoAnswer` | `call_.notify_hangup()` + بازگشت به `Ready` |
| `CallBusy` | `call_.notify_busy()` |
| `SmsReceived` (+CMTI) | با `auto_read_` → صف `read_sms(index)` |
| `SimReady` / `SimInserted` | `sim_ready=true`، خروج از `SimMissing`، callback `on_sim_` |
| `SimRemoved` | `enter(SimMissing)` + callback بحرانی |
| `NetworkReg` (کد 1/5) | `network_ready=true` و ورود به `Configuring` |
| `Ussd` | `ussd_.notify_cusd(line)` |
| `PowerDown` | `handle_power_down()`: حین PWRKEY → پولس روشن کردن؛ بعد از `AT`/`RDY` → URC کهنه (نادیده)؛ در Ready → `power_cycle()`؛ بدون پین PWRKEY → `enter(Off)` |

در پایان، هر URC به `user_urc_(ev)` هم داده می شود تا اپلیکیشن بتواند گوش کند.

### `void Modem::on_line(...)` / `on_final(...)`
- در `WaitingRegistration` پاسخ `+CREG?`/`+CGREG` پارس و با ثبت شبکه به `Configuring` می رویم (timeout ثبت: `GSM_REGISTRATION_TIMEOUT = 45s` → `Fault`).
- در `SimMissing` خط `+CPIN: READY` یعنی بازگشت سیم کارت → `WaitingRegistration`.
- `on_final` در `Booting`: موفق → `WaitingRegistration`؛ سه پاسخ ناموفق `AT` → `PowerPulsing` (حداکثر ۲ پالس PWRKEY) و سپس `Fault`.
- در `Configuring`: نتیجه هر گام بوت در پرچم های `session_` (`echo_off`، `text_mode`، `charset`، `csmp_ucs2`، `storage_ready`) ثبت می شود تا سرویس ها فرمان تکراری نفرستند؛ پایان جدول → `Ready` + `refresh_status()`.

### `void Modem::pump_services()` — رفع گیرکردن صف (v0.4)
سه شاخه:

```text
1) job جاری Running؟ ──► svc->pump()  یا  finish_job(svc->result())
2) job جاری Queued؟
      ├─ گیت تماس و job غیر-تماسی؟ ──► return (نگه داشته می شود؛ صف زنده است)
      ├─ start_job موفق؟ ──► شروع اجرا (start_failures_ = 0)
      └─ شکست: ++start_failures_ تا GSM_JOB_START_RETRIES (3)
             ──► fail با نتیجه سرویس یا kErrJobStalled؛ صف دوباره آزاد می شود
3) job جاری None؟ ──► queue_.pop → گیت تماس؟ (بازگشت به Queued) : start_job
```

قبل از این اصلاح، jobی که سرویسش (مثلاً به دلیل state ناسازگار) حاضر به شروع نبود برای همیشه جلوی صف را می گرفت؛ حالا حداکثر ۳ تلاش و سپس `kErrJobStalled` (`-102`) و ادامه کار صف.

### `void Modem::pump_housekeeping()`
کارهای دوره ای (فقط وقتی گیت تماس باز است): `full_sweep_ms` (وضعیت کامل)، `signal_ms` (CSQ)، `sms_scan_ms` (اسکن پیامک های خوانده نشده)، `cleanup_ms` (حذف پیامک های خوانده شده اگر مخزن پر باشد).

### `void Modem::check_watchdog()` — سگ نگهبان صنعتی
1. **پاک سازی jobهای کهنه:** `purge_stale(now, GSM_JOB_MAX_AGE_MS=45s)`؛ jobهای معلق مانده بیش از ۴۵ ثانیه با callback لغو حذف می شوند.
2. **بازیابی پس از timeout پیاپی (≥۳):** `AT+CFUN=1,1` (با ESC قبلش). اگر پروب `AT` بعد از CFUN جواب نداد → پولس PWRKEY.
3. **گیرکردگی در `Fault`/`SimMissing` بیش از `GSM_HARDWARE_REBOOT_TIMEOUT_MS` (۹۰s):** power-cycle کامل سخت افزاری.

### `void Modem::handle_sim_recovery()`
ماشین بازیابی سیم کارت در وضعیت `SimMissing`:
- هر `GSM_SIM_RETRY_INTERVAL_MS` (۱۰s): پروب `AT+CPIN?`.
- هر `GSM_CFUN_CYCLE_INTERVAL_MS` (۲۵s): چرخه `AT+CFUN=0` (فاز ۱) سپس `AT+CFUN=1` (فاز ۲) برای راه اندازی مجدد لایه RF/SIM.

### `void Modem::tick()` — حلقه اصلی
هر بار فراخوانی در حلقه اپلیکیشن:

```text
at_.tick()                    ← پردازش بایت ها/خط ها/نتایج
flush_cancelled()             ← تحویل callback های لغوشده
check_watchdog()              ← پاک سازی کهنه + بازیابی خطای صنعتی
call_.monitor(now, at_.idle()) + call_.pump()   ← سرپرستی تماس (مستقل از صف)
maintenance_service()         ← بازنشاننده های دوگانه watchdog
┌─ FSM ────────────────────────────────────────────────┐
│ Off/Fault        → return                            │
│ SimMissing       → handle_sim_recovery()             │
│ Booting          → اگر POWER DOWN دیده شد پولس روشن کردن؛ وگرنه پروب "AT" │
│ PowerPulsing     → پالس PWRKEY → Booting             │
│ WaitingRegistration → "AT+CREG?" هر 2s (timeout 45s) │
│ Configuring      → اجرای جدول kBoot                  │
└──────────────────────────────────────────────────────┘
if (at_.idle()):
    stat_.pump() / pump_services()
    به روزرسانی وضعیت FSM (InCall/Ringing/Ready) + pump_housekeeping()
    watchdog_.pump(...)      ← پروب های سلامت فقط در idle کامل
```

## جریان کاری

```text
        enqueue(job) ──► JobQueue ──► pop ──► current_
                                             │
                          ┌── call_gate() و job غیر-تماسی؟ ──► نگه داشته (Queued)
                          │
                          ▼
                     start_job() ──► سرویس (SMS/Call/Stat/USSD).pump()
                          │                │
        شکست start ×3 ──► kErrJobStalled   ▼
                          │          on_final / result
                          ▼                │
                     finish_job() ◄────────┘
                          │  retry تا max_attempts؟ ──► بازگشت به صف
                          ▼
              Succeeded / Failed / Cancelled + note_operation()
                          │
                          ▼
        watchdog budgets ──► maintenance_service ──► full_restart(keep_jobs=true)
```

## نکات فنی

- **گیت تماس ≠ توقف صف:** jobهای متوقف شده با `deferred` علامت می خورند و بعد از تماس بدون مصرف بودجه تلاش از سر گرفته می شوند؛ callback هرگز گم نمی شود.
- **`kErrJobStalled = -102`:** کد داخلی برای «job قابل شروع نبود»؛ مثل `kErrSmsTooLong` retry نمی شود تا صف هرگز wedge نشود.
- **`preempt_for_call` فقط job جاری را لغو می کند** نه صف را؛ ترکیب با گیت تماس یعنی «تماس وسط ارسال پیامک» هم امن است (payload با ESC لغو می شود و بعداً دوباره ارسال می شود).
- **`full_restart(keep_jobs=true)`** با reset کامل watchdog سازگار است: budgetهای زمانی/عملیاتی صفر می شوند ولی درخواست های کاربر از بین نمی روند.
- **`session_` (SessionState):** کش وضعیت مودم (echo/CMGF/CSCS/CSMP/CPMS) که با هر reset یا `fail` سرویس SMS باطل می شود؛ از فرمان های تنظیمی تکراری جلوگیری می کند.
- **ترتیب در tick مهم است:** سرپرستی تماس قبل از FSM و `pump_services` صدا زده می شود تا تایمرهای قطع/`+CLCC` همیشه به موقع برسند حتی وقتی صف شلوغ است.
- **شماره گذاری JobId:** از ۱ شروع می شود و صفر (`kInvalidJob`) هرگز برنمی گردد؛ پس از سرریز `uint16_t` به ۱ می پرد.
- **`GSM_JOB_MAX_AGE_MS = 45000`:** جلوی انباشت صف وقتی مودم آفلاین است را می گیرد؛ jobهای کهنه با حالت `Cancelled` به callback گزارش می شوند.

## ارتباط با سایر فایل ها

- `gsm/modem/modem.hpp` — تعریف `ModemState`، `Modem` و API عمومی.
- `gsm/queue/job.hpp` + `src/job_queue.cpp` — ساختار `Job`، `JobKind`، `JobState` و صف حلقوی.
- `src/at_engine.cpp` — موتور فرمان AT (عضو `at_`).
- `src/sms_service.cpp`، `src/call_service.cpp`، `src/status_service.cpp`، `src/ussd_service.cpp` — سرویس های تخصصی.
- `src/urc_router.cpp` — دسته بندی URCها (`UrcEvent`) که به `handle_urc` می رسند.
- `src/watchdog.cpp` — `MaintenanceWatchdog` (بودجه های reset + بررسی سلامت).
- `gsm/hal/hal.hpp` — `IUart`، `IClock`، `IPowerControl` (PWRKEY).
- `gsm/device/device_traits.hpp` — دستورهای اختصاصی هر مودم (CSMP، storage، hangup و …).
- `include/gsm/gsm_config.hpp` — همه ثابت های زمان بندی و ظرفیت.
