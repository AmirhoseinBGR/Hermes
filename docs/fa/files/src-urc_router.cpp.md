# gsm/src/urc_router.cpp

## نقش فایل

پیاده‌سازی `gsm::UrcRouter`؛ مسیریاب پیام‌های ناخواسته (URC) مودم. هر خط دریافتی از UART را دسته‌بندی می‌کند (`Urc`) و در صورت شناسایی، یک `UrcEvent` شامل نوع، متن خام و آرگومان عددی به handler مرکزی (`Modem::handle_urc`) تحویل می‌دهد. خطوط URC **هرگز** به observer فرمان جاری نمی‌رسند؛ این تفکیک، ستون فقرات تمایز «پاسخ فرمان» از «رویداد ناخواسته» در کل کتابخانه است.

## توابع و منطق کلیدی

### `Urc UrcRouter::classify(const char* line, int32_t& arg)`
دسته‌بندی یک خط؛ `arg` فقط برای رویدادهای عددی پر می‌شود (پیش‌فرض `-1`). الگوها به‌ترتیب بررسی می‌شوند:

| الگوی خط | `Urc` | `arg` |
|---|---|---|
| `RING` | `Ring` | — |
| `+CLIP:` | `CallerId` | — |
| `NO CARRIER` | `CallEnded` | — |
| `BUSY` | `CallBusy` | — |
| `NO ANSWER` | `NoAnswer` | — |
| `+CMTI:` | `SmsReceived` | ایندکس پیامک |
| `+CDSI:` / `+CDS:` | `SmsDeliveryRep` | — |
| `+CPIN: READY` | `SimReady` | — |
| `+CPIN: NOT READY` / `NOT INSERTED` | `SimRemoved` | — |
| `+CPIN:` (سایر: SIM PIN/PUK و …) | `SimReady` | — |
| `+SIM: 0` / `+QSIM: 0` | `SimRemoved` | — |
| `+SIM: 1` / `+QSIM: 1` | `SimInserted` | — |
| `Call Ready` / `SMS Ready` | `CallReady` / `SmsReady` | — |
| `NORMAL POWER DOWN` / `POWERED DOWN` | `PowerDown` | — |
| `+CREG:` / `+CGREG:` | `NetworkReg` | کد ثبت (فیلد ۱ یا ۰) |
| `+CUSD:` | `Ussd` | — |
| `RDY` / `DY` | `ModemReady` | — |
| سایر | `Unknown` | — |

نکته: برای `+CREG:`/`+CGREG:` آرگومان با `csv_field` از بعد از `:` استخراج می‌شود (ابتدا فیلد ۱، در صورت شکست فیلد ۰ — سازگار با هر دو قالب `<stat>` و `<n>,<stat>`).

### `bool UrcRouter::dispatch(const char* line, uint16_t len)`
خط را `classify` می‌کند؛ اگر `Unknown` باشد `false` (یعنی خط، پاسخ عادی فرمان است و باید به observer برسد). در غیر این صورت `UrcEvent{kind, line, arg}` ساخته، handler را صدا می‌زند و `true` برمی‌گرداند (خط «مصرف» شد).

```cpp
const Urc kind = classify(line, arg);
if (kind == Urc::Unknown) return false;
UrcEvent ev{kind, line, arg};
if (handler_) handler_(ev);
return true;
```

## جریان کاری

```text
AtEngine::tick ──► خط کامل ──► classify_final؟ (OK/ERROR/…) ──► نتیجه فرمان
                        │
                        └─► urc_.dispatch(line)
                                ├─ Unknown ──► false ──► observer_->on_line()  (پاسخ فرمان)
                                └─ شناخته‌شده ──► handler_(UrcEvent) ──► Modem::handle_urc
                                                        │
                                                        ├─ Ring/CallerId → preempt_for_call + call_.notify_ring
                                                        ├─ CallEnded/Busy/NoAnswer → پایان تماس
                                                        ├─ SmsReceived → read_sms خودکار
                                                        ├─ Sim* → وضعیت سیم‌کارت
                                                        ├─ NetworkReg → گذار FSM
                                                        ├─ Ussd → ussd_.notify_cusd
                                                        └─ user_urc_(ev) → اپلیکیشن
```

## نکات فنی

- **ترتیب بررسی مهم است:** `classify` اول «دقیق‌ترین» الگوها را می‌سنجد (مثلاً `+CPIN: READY` قبل از `+CPIN:` عمومی) تا دسته‌بندی‌های کلی، موارد خاص را نبلعند.
- **`NO CARRIER` دو هویتی:** در `AtEngine::classify_final` نتیجه نهایی فرمان (مثلاً `ATD`) و در اینجا URC پایان تماس است؛ اولویت با `classify_final` است (در `tick` اول نتیجه نهایی بررسی می‌شود)، پس وقتی observer فعالی در انتظار نتیجه است، `NO CARRIER` به `on_final(NoCarrier)` می‌رود نه URC.
- **`+CPIN`های مبهم:** هر حالت `+CPIN:` غیر از NOT READY/NOT INSERTED به‌عنوان `SimReady` گزارش می‌شود (حتی SIM PIN)؛ `StatusService` ریزبینانه‌تر تفسیر می‌کند.
- **`RDY`/`DY`:** برخی مودم‌ها با banner کوتاه `DY` (بخشی از `RDY` با تأخیر بافر) اعلام آمادگی می‌کنند؛ الگوی `DY` برای همین حالت عجیب اضافه شده است.
- **بدون heap:** `UrcEvent` فقط اشاره‌گر به `line` (بافر `LineReader`) و یک `int32_t` است؛ handler باید در همان لحظه مصرف کند یا کپی بگیرد.
- **`dispatch` مالکیت handler را ندارد:** handler با `set_handler(UrcRouter::Handler(...))` (Delegate ذخیره‌ای) نصب می‌شود؛ `Modem` خودش را با trampoline وصل می‌کند.

## ارتباط با سایر فایل‌ها

- `gsm/core/urc_router.hpp` — تعریف `Urc`، `UrcEvent`، `Handler` و `classify`.
- `src/at_engine.cpp` — نقطه فراخوانی `dispatch` در `tick` (قبل از `observer_->on_line`).
- `src/modem.cpp` — `urc_trampoline` و `handle_urc` (مصرف‌کننده اصلی رویدادها).
- `src/ussd_service.cpp` — `notify_cusd` برای `Urc::Ussd`.
- `src/call_service.cpp` — `notify_ring`/`notify_hangup`/`notify_busy` برای رویدادهای تماس.
- `gsm/util/static_string.hpp` — `str_starts_with`، `parse_int`، `csv_field`.
