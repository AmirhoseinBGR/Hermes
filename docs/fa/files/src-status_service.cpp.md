# gsm/src/status_service.cpp

## نقش فایل

پیاده‌سازی `gsm::StatusService`؛ سرویس جمع‌آوری وضعیت مودم: قدرت سیگنال (`AT+CSQ`)، ثبت شبکه (`AT+CREG?`)، وضعیت سیم‌کارت (`AT+CPIN?`)، اشغال مخزن پیامک (`AT+CPMS?`) و نام اپراتور (`AT+COPS?`). نتایج در ساختار مشترک `ModemStatus` (که URCها هم آن را به‌روز می‌کنند) نوشته می‌شود. تشخیص اپراتورهای ایرانی (Irancell / MCI / RighTel) برای انتخاب کد USSD موجودی هم در همین فایل است.

## توابع و منطق کلیدی

### توابع متنی
- `to_string(RegState)` → `"NotRegistered" | "Home" | "Searching" | "Denied" | "Unknown" | "Roaming"`
- `to_string(SimState)` → `"Unknown" | "Ready" | "Removed" | "LockedPin" | "LockedPuk" | "Error"`
- `to_string(Operator)` → `"Irancell" | "MCI (Hamrahe Aval)" | "RighTel" | "Unknown"`
- `to_string(StatusStep)` → `"csq" | "creg" | "cpin" | "cpms" | "cops"` (+ `"idle"/"done"`)

### `Operator detect_operator(const char* name)`
تشخیص اپراتور از نام گزارش‌شده `AT+COPS?` با جست‌وجوی زیررشته‌ها:

| اپراتور | نشانه‌ها |
|---|---|
| Irancell | `Irancell`، `MTN`، `IRANCELL`، `43235` |
| MCI (همراه اول) | `MCI`، `TCI`، `IR-MCI`، `IR-TCI`، `Hamrah`، `43211` |
| RighTel | `Rightel`، `RighTel`، `RIGHTEL`، `43220` |

کدهای MCC/MNC ایران هم (به‌صورت رشته در نام) لحاظ شده‌اند.

### `bool StatusService::start(StatusStep first)` / `start_full_sweep()`
شروع یک مرحله تکی یا یک sweep کامل (`Signal → Registration → SimPin → Storage → Operator`). اگر سرویس مشغول باشد `false`. `cancel()` فرمان جاری را با `at_.abort()` لغو می‌کند.

### `void StatusService::pump()`
صدور فرمان مرحله جاری (فقط وقتی فرمانی در پرواز نیست):

| گام | فرمان | timeout |
|---|---|---|
| `Signal` | `AT+CSQ` | ۱۰۰۰ms |
| `Registration` | `AT+CREG?` | ۱۰۰۰ms |
| `SimPin` | `AT+CPIN?` | ۵۰۰۰ms |
| `Storage` | `AT+CPMS?` | ۲۰۰۰ms (skip اگر `!traits_.storage_cmd`) |
| `Operator` | `AT+COPS?` | ۵۰۰۰ms |

### `void StatusService::on_line(const char* line, uint16_t len)`
پارس پاسخ‌ها و نوشتن مستقیم در `status_`:
- **`+CSQ: <rssi>,<ber>`** — `rssi_dbm = -113 + 2×rssi` (مقدار ۹۹ یعنی نامشخص → 0)؛ `ber` در فیلد دوم.
- **`+CREG: <n>,<stat>`** — فیلد ۱: `registration`؛ `network_ready = (stat == 1 || stat == 5)`.
- **`+CPIN: …`** — دسته‌بندی: `NOT READY`/`NOT INSERTED` → `Removed`؛ `READY` → `Ready`؛ `SIM PIN` → `LockedPin`؛ `SIM PUK` → `LockedPuk`؛ غیره → `Error`. به‌روزرسانی `sim_ready`.
- **`+CPMS: "…",used,total,…`** — فیلدهای ۱ و ۲: `sms_used` / `sms_total`.
- **`+COPS: <mode>,<format>,"name"`** — فیلد ۲: نام اپراتور + `detect_operator`.

### `void StatusService::on_final(const AtResult& res)`
نتیجه هر فرمان در `result_` و زمان به‌روزرسانی در `status_.updated_at_ms` ثبت می‌شود. اگر sweep کامل نباشد (`!sweep_`) سرویس تمام می‌شود؛ وگرماه به گام بعدی می‌رود و در پایان خلاصه‌ای از همه مقادیر (`csq`, `reg`, `sim`, `sms`, `op`) لاگ می‌شود.

## جریان کاری

```text
start_full_sweep()
   │
   ▼
[Signal] ─AT+CSQ──► [Registration] ─AT+CREG?──► [SimPin] ─AT+CPIN?──►
   │
   ▼
[Storage] ─AT+CPMS?──► [Operator] ─AT+COPS?──► [Idle]
   │                                             │
   └────────── هر on_final: status_.updated_at_ms ┘
                      └─► ModemStatus زنده ─► کنسول/CLI/watchdog
```

## نکات فنی

- **اشتراک `ModemStatus` با URCها:** `UrcRouter` خطوط `+CREG`/`+CPIN` ناخواسته را هم در همین ساختار می‌نویسد؛ پس مقادیر همیشه «زنده»‌اند حتی وقتی sweep در جریان نیست (`MaintenanceWatchdog` برای همین به `status_` اشاره می‌کند).
- **باقی‌مانده‌های دیباک:** در پارسر `+CPIN` چند فراخوانی `printf` (مثل `printf("===> line: %s \n", line);`) باقی مانده که مستقیم به stdout می‌رود و از مسیر سیستم لاگ عبور نمی‌کند؛ در محصول نهایی باید حذف شود (در `src/status_service.cpp` در پردازش `+CPIN:`).
- **`+CPMS?` فقط خواندنی است:** `SmsService` مخزن را با `traits_.storage_cmd` (معمولاً `AT+CPMS=…`) تنظیم می‌کند؛ این سرویس فقط اشغال/ظرفیت را گزارش می‌دهد.
- **`rssi_dbm` فرمول استاندارد 3GPP است** (`-113 + 2×CSQ`)؛ `signal_bars()` در هدر `ModemStatus` آن را به ۰–۵ ستاره برای UI تبدیل می‌کند.
- **`storage_cmd` اختیاری:** برخی دستگاه‌ها `AT+CPMS` را پشتیبانی نمی‌کنند؛ گام `Storage` با کمک `DeviceTraits` skip می‌شود.
- **بدون heap:** بافرهای پارس (`f[8]`، `f[12]`، `f[32]`) آرایه‌های پشته‌ای کوچک‌اند.

## ارتباط با سایر فایل‌ها

- `gsm/status/status_service.hpp` — `StatusStep`، `RegState`، `SimState`، `Operator`، `ModemStatus`.
- `gsm/core/at_engine.hpp` — صدور فرمان‌ها به‌عنوان `IAtObserver`.
- `src/modem.cpp` — `refresh_status()` (job `StatusSweep`) و `pump_housekeeping` (سیکل‌های دوره‌ای) و `pump()` در `tick`.
- `src/watchdog.cpp` — خواندن `sim_ready`/`network_ready` از `ModemStatus` در بررسی سلامت.
- `src/ussd_service.cpp` — `default_balance_code(Operator)` از `detect_operator` تغذیه می‌شود.
- `src/cli.cpp` — نمایش اپراتور/سیگنال/SMS در نوار وضعیت و نمای زنده.
