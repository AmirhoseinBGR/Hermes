# gsm/src/watchdog.cpp

## نقش فایل

پیاده‌سازی `gsm::MaintenanceWatchdog`؛ نگهبان نگهداری (maintenance) مودم با دو «بودجه بازنشانی» مستقل و یک «بررسی سلامت» فعال. بازنشاننده #1 بر مبنای **زمان** (پیش‌فرض هر ۱ ساعت یک reset کامل ماژول) و بازنشاننده #2 بر مبنای **تعداد عملیات** (پیش‌فرض هر ۵۰ job تمام‌شده) کار می‌کند؛ پس از هر reset کامل، یک زنجیره پروب سلامت (`AT`، `AT+CPIN?`، `AT+CREG?`، `AT+CSQ`) وضعیت واقعی ماژول را با خواندن `ModemStatus` زنده (نگه‌داری‌شده توسط URCها) تأیید می‌کند. این همان «watchdog دوگانه» v0.4 است.

## توابع و منطق کلیدی

### توابع متنی
- `to_string(ResetReason)` → `"none" | "periodic" | "ops-budget" | "manual" | "fault-recovery"`
- `to_string(HealthState)` → `"unknown" | "checking" | "healthy" | "unhealthy"`

### `void MaintenanceWatchdog::note_operation()`
شمارنده عملیات (`ops_`) را یکی افزایش می‌دهد. `Modem::finish_job` این را برای هر job به‌سرانجام‌رسیده (موفق/ناموفق/لغوشده) صدا می‌زند.

### `void MaintenanceWatchdog::tick(uint32_t now_ms)` — ارزیابی دو بودجه
اگر غیرفعال باشد یا همین حالا یک reset در انتظار باشد، کاری نمی‌کند. در غیر این صورت:

```cpp
// Resetter #1: time budget (default 1h)
if (policy_.periodic_ms && started_ &&
    hal::elapsed(now_ms, last_reset_ms_, policy_.periodic_ms))
    request_reset(ResetReason::Periodic);

// Resetter #2: ops-count budget
if (policy_.ops_period && ops_ >= policy_.ops_period)
    request_reset(ResetReason::OpsBudget);
```

هر دو فقط یک «درخواست» ثبت می‌کنند (`pending_`)؛ اجرای واقعی reset بر عهده `Modem::maintenance_service` است که پنجره امن پیدا می‌کند.

### `void MaintenanceWatchdog::request_reset(ResetReason r)` / `ResetReason take_pending()`
درخواست reset با اولویت «اولی برنده»؛ تا وقتی `pending_` تخلیه نشده درخواست جدید نادیده گرفته می‌شود. `take_pending` مقدار را می‌خواند و پاک می‌کند (یک‌بارمصرف).

### `void MaintenanceWatchdog::on_reset_started(uint32_t now_ms)`
پس از هر reset کامل توسط `Modem::begin` صدا زده می‌شود: ساعت بودجه زمانی تازه (`last_reset_ms_`)، `ops_ = 0`، `started_ = true`، شمارنده `resets_++` و یک `HealthReport` تازه (با ذکر تعداد resetها). اگر `policy_.health_check` فعال باشد، `check_pending_ = true` می‌شود تا به‌محض بیکار شدن سیستم، بررسی سلامت اجرا شود.

### `void MaintenanceWatchdog::start_check()` / `cancel_check()`
شروع/لغو بررسی سلامت. `start_check` شمارنده پروب را صفر و وضعیت را `Checking` می‌کند؛ `check_busy()` همین وضعیت را گزارش می‌دهد.

### `void MaintenanceWatchdog::pump(AtEngine& at, bool engine_idle, bool modem_ready)`
موتور بررسی سلامت؛ فقط وقتی سیستم کاملاً بیکار است (موتور AT آزاد، هیچ job در حال اجرا نیست و مودم در وضعیت `Ready` است) پروب بعدی را می‌فرستد:

```text
probe 0: "AT"        (1000 ms)   → at_ok
probe 1: "AT+CPIN?"  (3000 ms)   → از ModemStatus زنده خوانده می‌شود
probe 2: "AT+CREG?"  (2000 ms)   → از ModemStatus زنده خوانده می‌شود
probe 3: "AT+CSQ"    (2000 ms)   → rssi از خط +CSQ:
probe 4: → finish_check()
```

نکته: پاسخ‌های `+CPIN` / `+CREG` توسط `UrcRouter` مصرف و در `ModemStatus` زنده به‌روز می‌شوند؛ به همین دلیل `on_line` فقط `+CSQ` را مستقیم می‌خواند و بقیه از `status_` (اشاره‌گر به `ModemStatus` متصل‌شده با `bind_status`) برداشت می‌شوند.

### `void MaintenanceWatchdog::on_line(const char* line, uint16_t len)`
فقط در پروب ۳ و فقط برای `+CSQ:`: `rssi_dbm = -113 + 2 × rssi` (مقدار ۹۹ یعنی نامشخص و نادیده گرفته می‌شود).

### `void MaintenanceWatchdog::on_final(const AtResult& res)`
پایان هر پروب: اگر پروب ۰ (`AT`) موفق بود `health_.at_ok = true`؛ سپس `probe_index_++` (صرف‌نظر از نتیجه، تا زنجیره همیشه جلو برود).

### `void MaintenanceWatchdog::finish_check()`
نتیجه نهایی را می‌سازد:

```cpp
health_.sim_ok = status_->sim_ready;
health_.net_ok = status_->network_ready;
const bool ok = health_.at_ok && health_.sim_ok && health_.net_ok;
health_.state = ok ? HealthState::Healthy : HealthState::Unhealthy;
```

سپس لاگ (`health: OK (rssi=…, resets=…)` یا `health: FAILED (at=… sim=… net=…)`) و فراخوانی `on_health_(health_)`.

## جریان کاری

```text
Modem::finish_job ──► note_operation() ──► ops_++
Modem::tick ──► watchdog_.tick(now)
                   │
                   ├─ periodic_ms سپری؟ ──► request_reset(Periodic)
                   └─ ops_ >= ops_period؟ ──► request_reset(OpsBudget)
                            │ (pending_)
                            ▼
        Modem::maintenance_service ──► پنجره امن؟ ──► take_pending()
                            │                              │
                            │                              ▼
                            │                   full_restart(keep_jobs=true)
                            │                              │
                            └──────────────────► on_reset_started(): بودجه‌ها صفر
                                                       │ health_check؟
                                                       ▼
                            pump(): AT ─► CPIN? ─► CREG? ─► CSQ ─► finish_check()
                                                       │
                                                       ▼
                                            on_health_(HealthReport)
```

## نکات فنی

- **بودجه‌ها «درخواست» می‌دهند نه اجرا:** `tick` هرگز مستقیم reset نمی‌کند؛ `Modem::maintenance_service` با قانون grace (`GSM_WATCHDOG_GRACE_MS = 10s`) منتظر پنجره امن (بیکاری کامل) می‌ماند و اگر پنجره پیدا نشد (مثلاً تماس گیرکرده) پس از grace به‌صورت «اجباری» اجرا می‌کند.
- **بودجه #1 (زمان):** `GSM_PERIODIC_RESET_MS = 3600000` (۱ ساعت). مقدار ۰ یعنی غیرفعال. هدف: پاک‌سازی تجمعی خطاها/نشت‌های داخل firmware مودم در اپلیکیشن‌های ۲۴/۷.
- **بودجه #2 (تعداد عملیات):** `GSM_OPS_RESET_COUNT = 50`. برای سنگین‌ترین چرخه‌ها (مثلاً ارسال انبوه پیامک) زودتر از بودجه زمانی فعال می‌شود. ۰ یعنی غیرفعال.
- **`use_power_cycle`:** پیش‌فرض `false` — ریست نگهبان با `AT+CFUN=1,1`. اگر `true` و `IPowerControl` موجود باشد، پالس PWRKEY.
- **HealthReport:** `state`، `at_ok`، `sim_ok`، `net_ok`، `rssi_dbm`، `resets` و `since_reset_ms`؛ متد `ok()` فقط در حالت `Healthy` درست است.
- **پروب‌ها کم‌هزینه‌اند:** هر پروب یک فرمان AT ساده است و فقط در idle کامل ارسال می‌شود؛ هیچ رقابتی با jobهای کاربر یا سرویس تماس وجود ندارد.
- **بدون heap:** همه stateها عضو کلاس و `uint8_t/uint32_t` هستند؛ `HealthReport` هم POD است.
- **`cancel_check` اضطراری:** اگر وسط بررسی، رویداد مهمی (مثلاً تماس) رخ دهد، `Modem` می‌تواند بررسی را رها کند؛ `health_` به حالت اولیه بازمی‌گردد.

## ارتباط با سایر فایل‌ها

- `gsm/maintenance/watchdog.hpp` — `ResetReason`، `HealthState`، `HealthReport`، `ResetPolicy`.
- `src/modem.cpp` — `maintenance_service()` (پنجره امن و اجرای reset)، `tick()` (فراخوانی `pump` در idle)، `check_watchdog()` (بازیابی خطاهای صنعتی مکمل).
- `src/at_engine.cpp` — صدور پروب‌ها به‌عنوان `IAtObserver`.
- `gsm/status/status_service.hpp` — ساختار `ModemStatus` که `sim_ready` / `network_ready` آن مرجع تأیید سلامت است.
- `src/log.cpp` — کانال لاگ `"watchdog"`.
- `include/gsm/gsm_config.hpp` — `GSM_PERIODIC_RESET_MS`، `GSM_OPS_RESET_COUNT`، `GSM_WATCHDOG_GRACE_MS`.
- `src/cli.cpp` — پنل `w` (نمایش سیاست/شمارنده‌ها) و دستورهای `health` / `wreset`.
