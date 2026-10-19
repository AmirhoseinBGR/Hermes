# معماری کتابخانه GSM (SIM800 / Quectel M08 / M08R)

هدف: یک درایور غیرمسدودکننده (non-blocking) با `tick()`، ماشین حالت اصلی + زیرحالت،
صف کار، و پیش فرض «SMS فارسی در Text Mode با UCS2». بدون STL، بدون heap،
بدون exception/RTTI.

## ۱) لایه بندی

```
L4  app                      ← کد شما (فقط با Modem حرف می زند)
      └── gsm::cli::Console  ← کنسول تعاملی (قابلیت کتابخانه، اختیاری)
--------------------------------------------------------------
L3  Modem                    main FSM + JobQueue + Housekeeping + گیت تماس
    ├── SmsService           زیرحالت ارسال/خواندن/حذف + گارد طول پیام
    ├── CallService          سرپرستی تماس (تایمر قطع خودکار + پرس وجوی +CLCC)
    ├── StatusService        CSQ / CREG / CPIN / CPMS / COPS
    ├── UssdService          کدهای USSD و استعلام شارژ
    └── MaintenanceWatchdog  دو ریست کننده دوره ای + بررسی سلامت
--------------------------------------------------------------
L2  AtEngine                 یک فرمان در پرواز، تایم اوت، prompt، ESC
    ├── LineReader           بایت → خط (و تشخیص «> » بدون terminator)
    └── UrcRouter            RING / +CMTI / NO CARRIER / ...
--------------------------------------------------------------
L1  codec (UCS2, PhoneNumber) + log + util (StaticString, RingBuffer, Delegate)
--------------------------------------------------------------
L0  hal::IUart / IClock / IPowerControl   ← تنها نقطه ی وابستگی به پلتفرم
    ports/stm32 (پیش فرض) · ports/arduino · ports/host (تست دسکتاپ)
```

قاعده ی طلایی: **هیچ لایه ای به لایه ی بالاتر وابسته نیست** و فقط `AtEngine` با UART حرف می زند.
پورت جدید = پیاده سازی سه اینترفیس L0. ماژول جدید = یک `DeviceTraits` جدید.

## ۲) ماشین حالت

حالت اصلی (`ModemState`):

| حالت | معنی | خروج |
|---|---|---|
| `Off` | خاموش/شروع نشده | `begin()` |
| `Booting` | ارسال `AT` تا جواب دادن ماژول (۱۰ تلاش) | پاسخ `OK` |
| `Configuring` | اجرای لیست bootstrap (ATE0, CMEE, CLIP, CNMI, CMGF, CSCS, CSMP, CPMS) | پایان لیست |
| `Ready` | بی کار؛ فقط اینجا housekeeping اجرا می شود | آمدن Job یا URC |
| `Working` | یک Job در جریان است (`activity()` + `step()`) | پایان Job |
| `Ringing` | تماس ورودی در انتظار | `answer()` / `hang_up()` / `NO CARRIER` |
| `InCall` | تماس برقرار | قطع تماس |
| `Fault` | تسلیم؛ نیاز به `reset()` | `reset()` |

زیرحالت ها داخل سرویس ها زندگی می کنند (`SmsStep`, `CallStep`, `StatusStep`, `CallState`).
مثلاً ارسال پیامک: `cmgf → cscs → csmp → cpms → cmgs-header → payload → wait-cmgs`.
قدم های کانفیگ اگر قبلاً اعمال شده باشند (`SessionState`) بدون ترافیک AT رد می شوند؛
یعنی پیامک دوم فقط `AT+CMGS` می فرستد. هر خطا `SessionState` را بی اعتبار می کند تا
دفعه ی بعد کانفیگ از نو اعمال شود.

## ۳) صف کار (JobQueue)

- ظرفیت ثابت `GSM_QUEUE_DEPTH` (پیش فرض ۶)، FIFO، بدون allocation.
- `urgent` (مثل `hang_up()` و `answer()`) از جلوی صف وارد می شود.
- retry داخلی هر Job (`max_attempts`؛ ارسال پیامک پیش فرض ۲).
- کنترل کلی: `pause_queue()` / `resume_queue()` / `stop_all()` / `cancel(id)`.
  `stop_all()` هم Job در حال اجرا را با ESC/ATH می بندد، هم بقیه را Cancelled می کند
  و کالبک ها را صدا می زند (هیچ Job بی جواب نمی ماند).
- وضعیت صف قابل مشاهده است: `queue_size()`, `queued(i)`, `current_job()`.

## ۴) حالت Idle / نگهبانی

در `Ready` و طبق `HousekeepingConfig`:
`signal_ms` (CSQ) · `full_sweep_ms` (CSQ+CREG+CPIN+CPMS+COPS) · `sms_scan_ms`
(`AT+CMGL="REC UNREAD"` و تحویل + حذف) · `cleanup_ms` (`AT+CMGD=1,1` پاک سازی خوانده شده ها).
همه چیز در `ModemStatus` ذخیره می شود (`csq_raw`, `rssi_dbm`, `signal_bars()`,
`registration`, `sim_ready`, `sms_used/total`, `operator_name`).

## ۵) پیامک فارسی (پیش فرض کل سیستم)

`SmsEncoding::Ucs2` پیش فرض است. جزئیات کشف شده از لاگ های خودت در
[`QUIRKS.md`](QUIRKS.md) کد شده اند. خلاصه: در `CSCS="UCS2"` هم متن **و هم شماره**
باید UCS2-hex باشند و بعد از payload هیچ CRLF قبل از Ctrl+Z نباید برود.

## ۶) لاگ

`gsm::log` جانشین `def::debug` است: سطح Off..Trace، sink قابل تعویض (stdio / writer /
null)، **رنگ ANSI به صورت runtime قابل خاموش شدن**، timestamp، `bytes()` برای دیدن
`\r\n`/`<CtrlZ>`/`<ESC>` و `hexdump()`، حذف کامل در کامپایل با `GSM_LOG_LEVEL_MAX`.

## ۷) مصرف حافظه (پیش فرض)

RX ring 512B + DMA 256B + صف (۶ × ~۲۳۰B) + inbox/بافرهای متن ≈ **۳ تا ۴ کیلوبایت RAM**،
همه استاتیک. با `GSM_*` قابل کوچک شدن است.

## ۸) نقطه های توسعه (بدون دست زدن به هسته)

- ماژول جدید (M66، A6، SIM7600): یک `DeviceTraits` اضافه کن.
- GPRS/TCP بعداً: `net/DataService : IService` + `JobKind::*` جدید. هسته دست نخورده می ماند.
- PDU mode: `codec/pdu.*` + یک `SmsEncoding::Pdu`.
- USSD / تماس چندطرفه / AT+CLCC polling: سرویس جدید در همان الگو.

## ۶) جریان گیت تماس و سرپرستی (v0.4)

```
Modem::tick() (هر تیک):
  at_.tick()                    ← خواندن UART و توزیع خطوط/URC
  check_watchdog()              ← واچ داگ صنعتی قبلی
  call_.monitor(now, idle)      ← تایمر قطع خودکار + پرس وجوی +CLCC
  maintenance_service()         ← دو ریست کننده دوره ای
  ... ماشین حالت اصلی ...
  pump_services()               ← اجرای صف (با گیت تماس)
  watchdog_.pump(...)           ← پروب های سلامت فقط وقتی همه چیز بیکار است
```

**گیت تماس**: وقتی `call_.state() != Idle` باشد، جاب های غیرتماسی شروع نمی شوند
(در صف یا `current_` نگه داشته می شوند). اگر تماس ورودی وسط یک جاب برسد،
`preempt_for_call()` جاب را با ESC به صورت امن لغو و `deferred` می کند؛
بعد از پایان تماس، جاب بدون از دست دادن بودجه تلاش مجدد از سر گرفته می شود.

**ضد گیر کردن صف**: اگر سرویسی از شروع جاب امتناع کند، چند بار تلاش و سپس
جاب با `kErrJobStalled` تمام می شود — صف هرگز نمی ایستد.

**ریست نگه بانی**: `full_restart(hardware, keep_jobs=true)` جاب های صف کشیده را
نگه می دارد؛ بعد از رسیدن به `Ready`، بررسی سلامت اجرا و نتیجه گزارش می شود.
