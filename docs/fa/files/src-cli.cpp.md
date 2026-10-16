# gsm/src/cli.cpp

## نقش فایل

پیاده‌سازی `gsm::cli::Console`؛ کنسول تعاملی صنعتی مودم GSM که در نسخه v0.4 از اپلیکیشن به داخل کتابخانه منتقل شده تا هر محصولی فقط با یک `Console` به منوی کامل، وضعیت زنده، نمای صف، پنل watchdog، پل وب و حالت passthrough دسترسی داشته باشد. کنسول ورودی بایت‌به‌بایت سریال را می‌گیرد، دستورهای تک‌حرفی و متنی را اجرا می‌کند، رویدادهای مودم (SMS، تماس، SIM، job، سلامت) را با رنگ‌بندی ANSI چاپ می‌کند و همه‌چیز را به‌صورت هم‌زمان به «پل وب» آینه می‌کند تا یک رابط تحت مرورگر هم بتواند همان دستورات را اجرا کند.

## توابع و منطق کلیدی

### لایه خروجی
- **`TextSink`** — سینک متنی عمومی با `put`/`printf` (بافر `vsnprintf` به اندازه ۱۹۲ بایت)؛ پایه همه خروجی‌های سریال.
- **`Console::out` / `outf`** — در حالت وب (`web_mode_`) خروجی به `web_out_chunk` می‌رود وگرنه به `out_`.
- **`web_out_chunk`** — آینه‌سازی خط‌به‌خط برای پل وب: `\r` حذف، `\n` یعنی ارسال یک خط کامل؛ بافر خط (`wline_`) با سرریز خودکار flush می‌شود.
- **`web_event`** — رویدادهای push (فقط وقتی خودِ وب دستور نداده) برای پنل مرورگر.
- **`log_thunk` / `install_log_mirror`** — سینک لاگ کتابخانه را طوری نصب می‌کند که هر رکورد لاگ هم‌زمان به سریال و پل وب برود (`log::Registry::set_sink(&log::writer_sink, …)`)؛ رنگ ANSI روشن، timestamp خاموش و سطح پیش‌فرض `Warn`.

### کمک‌های ANSI
پالت مینیمال (RST/BOLD/DIM و رنگ‌های عادی و پررنگ) به‌همراه:
- `ansi_bar(pct, w)` — نوار درصدی رنگی (قرمز < ۳۰٪، زرد < ۶۰٪، سبز) برای آنتن و مخزن SMS.
- `ansi_divider` / `ansi_clear` — جداکننده و پاک‌سازی صفحه (`ESC[2J` + `ESC[H`).
- `kv(key, val, color)` — جفت کلید/مقدار با ستون ۱۴ نویسه‌ای؛ ستون فقرات همه پنل‌ها.
- `prompt()` — نشان `gsm> `.

### نماها (Views)
- **`draw_status_bar`** — یک خط وضعیت: NET/اپراتور، SIG (dBm و ستاره)، SIM، SMS (استفاده/کل)، صف (با `!` زرد وقتی paused) و وضعیت تماس + FSM.
- **`draw_live_status`** — صفحه کامل «حالت زنده»: FSM + گام جاری (`activity()`/`step()`)، نوار سیگنال، ثبت شبکه، اپراتور، SIM، نوار مخزن SMS، وضعیت تماس + **`CallEndReason` آخرین تماس**، صف، watchdog (تعداد resetها و بودجه عملیات)، وضعیت سلامت + RSSI، سن داده (`updated_at_ms`) و در صورت فعال‌بودن `show_rate_` نرخ حلقه.
- **`draw_queue`** — وضعیت صف: paused/active، اندازه، job جاری با نشانگر `[held-for-call]` برای jobهای معطل‌مانده توسط گیت تماس، و فهرست jobهای در انتظار با نشان `URGENT`.
- **`draw_watchdog`** — پنل نگهداری: فعال/غیرفعال، `Reset #1 time` (بودجه زمانی)، `Reset #2 ops` (بودجه عملیات)، grace، حالت reset (PWRKEY/نرم)، شمارنده‌ها/آپ‌تایم، بودجه عملیات و گزارش سلامت (`at/sim/net/rssi`).
- **`draw_menu`** — منوی اصلی در چهار بخش: `[STATUS & INFO]` (i/s/q/r/d/b/w/health)، `[ACTIONS & PROTECTION]` (p/x/h/R/K/P/c)، `[POLICY]` (smsmax/callto/wreset) و `[LOGGING]` (L/T/C) + فهرست دستورهای متنی.
- **`redraw_current` / `redraw`** — بازکشیدن صفحه؛ در حالت وب repaint تمام‌صفحه انجام نمی‌شود (مخصوص سریال است).

### پردازش دستور: `void Console::handle(char* line)`
پس از trim فاصله‌ها، دستورها به‌ترتیب بررسی می‌شوند. در حالت وب، دستورهای تمام‌صفحه (`m`، `c`، `i`، `P`) با پیام راهنما رد می‌شوند چون متعلق به صفحه سریال‌اند.

**دستورهای تک‌حرفی:**

| دستور | عمل |
|---|---|
| `m` / `c` | منو / پاک‌سازی صفحه |
| `i` | وضعیت زنده (بازکشیدن خودکار هر ۲s) |
| `q` | نمای صف |
| `w` | پنل watchdog و سلامت |
| `health` | اجرای بررسی سلامت (`maintenance().start_check()`) |
| `wreset` | درخواست reset نگهداری دستی (`ResetReason::Manual`) |
| `s` | sweep کامل وضعیت (`refresh_status`) |
| `r` | اسکن پیامک‌های خوانده‌نشده |
| `d` | حذف پیامک‌های خوانده‌شده |
| `h` | قطع فوری تماس (urgent) |
| `b` | استعلام موجودی با USSD |
| `p` | توقف/ادامه صف |
| `x` | توقف همه و پاک‌سازی صف |
| `R` | reset نرم FSM |
| `K` | پالس PWRKEY (ریست سخت‌افزاری) |
| `P` | ورود/خروج حالت passthrough |
| `L` / `T` / `C` | چرخه سطح لاگ / timestamp / رنگ ANSI |

**دستورهای سیاست:** `smsmax <chars>` (تنظیم `max_chars` سرویس SMS) و `callto <ms>` (تنظیم هم‌زمان `dial_timeout_ms` و `ring_timeout_ms` در `CallPolicy`).

**دستورهای متنی:** `sms <num> <text>` (صف ارسال؛ خطای طول/صف پُر با پیام قرمز)، `dial <num>`، `ussd <code>`، `read <idx>`، `del <idx>` (حذف مستقیم از سرویس SMS، بیرون از صف!) و `at <cmd>` (ارسال فرمان AT خام با observer خودِ کنسول و timeout ۵s؛ اگر موتور مشغول باشد `AT engine busy!`).

### آینه‌های رویداد مودم (thunkها)
هر رویداد با Delegate به کنسول وصل می‌شود و هم به سریال و هم به پل وب گزارش می‌شود:
- **`on_sms_thunk`** — چاپ `[SMS] [index] from=…` + متن؛ **پاسخ خودکار `pong` به پیامک `ping`** (نمایش قابلیت ارسال از درون رویداد).
- **`on_call_thunk`** / **`on_call_ended_thunk`** — `[CALL] <state> peer=…` و `[CALL-END] <CallEndReason>`.
- **`on_sim_thunk`** — هشدار قرمز `CRITICAL: SIM CARD REMOVED!` یا پیام سبز `SIM CARD READY`.
- **`on_job_thunk`** — `[JOB#id] <kind> <state> code=…` به‌همراه پاسخ USSD در صورت موفقیت.
- **`on_health_thunk`** — `[HEALTH] <state> at=… sim=… net=… rssi=…`.

### چرخه حیات و ورودی
- **`init(bool draw_menu_now)`** — نصب آینه لاگ، سیم‌کشی همه callbackها و (در صورت درخواست) رسم منو.
- **`set_passthrough(bool)`** — جابه‌جایی به حالت پل مستقیم سریال ↔ مودم.
- **`feed(uint8_t byte)`** — ماشین ورودی:
  - حالت passthrough: هر بایت مستقیم به UART مودم؛ حرف `e` یعنی خروج.
  - `ESC` (0x1B): بازگشت به منو از هر نمایی.
  - `\r`/`\n`: اجرای خط جمع‌شده با `handle` (خط خالی = بازکشیدن نما).
  - `0x08`/`0x7F` (Backspace): حذف نویسه با اکوی `\b \b`.
  - نویسه‌های چاپی (≥ 0x20): انباشت در بافر `line_` با اکو.
- **`feed_line(const char*)`** — اجرای مستقیم یک خط متنی (برای ورودی‌های آماده).
- **`exec_web(const char* line)`** — نقطه ورود پل وب: دستور را با `web_mode_ = true` اجرا می‌کند تا خروجی به‌جای سریال به وب برود، سپس یک newline باقی‌مانده را flush می‌کند.
- **`status_line(char*, cap)`** — رشته وضعیت فشرده با جداکننده `|` (۱۶ فیلد: FSM/activity/step/ready/sim/net/operator/rssi/bars/sms_used/sms_total/queue…) برای polling صفحه وب.

### `void Console::tick()`
- شمارنده `loop_count_` برای محاسبه نرخ حلقه.
- در passthrough: خواندن تا ۶۴ بایت از UART مودم و چاپ مستقیم.
- در نمای زنده: بازکشیدن خودکار هر ۲۰۰۰ میلی‌ثانیه (`last_draw_`).

### `IAtObserver` (برای دستور `at`)
- `on_line` — چاپ خط پاسخ.
- `on_final` — چاپ نتیجه نهایی به شکل `<OK>` / `<TIMEOUT>` / …

## جریان کاری

```text
   سریال/TCP                    Console                         مودم
   ─────────                    ───────                         ─────
   بایت‌ها ──► feed() ──► خط کامل ──► handle()
                                        │
              ┌─────────────────────────┼──────────────────────────┐
              ▼                         ▼                          ▼
         نماها (i/q/w)          دستور عملیاتی              دستور متنی
      draw_live_status          p/x/h/R/K/health           sms/dial/ussd/at
              │                         │                          │
              │                         ▼                          ▼
              │                    modem API  ◄──── JobQueue ──── سرویس‌ها
              │                         │
   رویدادها: ◄┴── on_*_thunk ◄── callbackهای مودم (SMS/Call/SIM/Job/Health)
        │
        ├──► سریال (ANSI)
        └──► web_out_chunk / web_event ──► پل وب (exec_web در جهت معکوس)
```

## نکات فنی

- **کنسول به‌عنوان قابلیت کتابخانه:** همه stateها (بافر خط، حالت‌ها، پرچم وب) داخل `Console` است؛ `init()` هیچ وابستگی به اپلیکیشن ندارد و API با `feed`/`tick`/`exec_web`/`status_line` کامل می‌شود.
- **بدون heap:** بافرهای `printf` (۱۹۲)، `web_event` (۱۷۶)، خط وب و `line_` همگی ثابت‌اند؛ برای میکروکنترلر مناسب است.
- **آینه لاگ سراسری است:** `install_log_mirror` سینک `log::Registry` را عوض می‌کند؛ اگر اپلیکیشن سینک خودش را بخواهد باید بعد از `init` این کار را بکند (`ScopedSink` در log.cpp برای همین است).
- **رنگ‌بندی وابسته به `log::Registry::color()`** است؛ دستور `C` آن را کلی خاموش/روشن می‌کند (برای ترمینال‌های بدون ANSI).
- **`del <idx>` خارج از صف** مستقیماً `sms().start_delete_index` را صدا می‌زند؛ اگر سرویس مشغول باشد بی‌صدا نادیده گرفته می‌شود — تفاوت عمدی با `read` که job می‌سازد.
- **پل وب خط‌oriented است:** `web_out_chunk` بافر خط دارد و repaintهای تمام‌صفحه (`m`/`c`/`i`) در حالت وب غیرفعال‌اند تا پروتکل ساده بماند.
- **نشانگر `[held-for-call]`** در نمای صف، jobهای معطل‌شده توسط گیت تماس (قابلیت v0.4 مودم) را نمایان می‌کند.
- **`callto`** هر دو تایمر `CallPolicy` (MO و MT) را با هم تنظیم می‌کند؛ برای تفکیک باید از API مستقیم `modem().call_policy()` استفاده شود.

## ارتباط با سایر فایل‌ها

- `gsm/cli/cli.hpp` — تعریف `Console`، `TextSink`، `Mode` و `WriterSink` لاگ.
- `src/modem.cpp` — همه عملیات‌ها از طریق API مودم (`send_sms`، `dial`، `hang_up`، `refresh_status`، `stop_all`، `reset`، `power_cycle`، `at()` و …).
- `src/call_service.cpp` — `CallPolicy` (دستور `callto`)، `CallState`/`CallEndReason` در نماها.
- `src/sms_service.cpp` — `set_max_chars` (دستور `smsmax`) و حذف مستقیم.
- `src/watchdog.cpp` — پنل `w`، دستورهای `health` / `wreset` و گزارش `HealthReport`.
- `src/job_queue.cpp` — `JobQueue::capacity` و نمایش صف.
- `src/log.cpp` — `log::Registry`، `writer_sink` و سطح‌های لاگ (دستور `L`).
- `gsm/tools/serial_bridge.hpp` — نمونه اتصال سریال/وب که `feed`/`exec_web` را تغذیه می‌کند.
