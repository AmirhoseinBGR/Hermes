# gsm/include/gsm/queue/job.hpp

## نقش فایل

تعریف «Job» — واحد کاری صف شده در مودم. هر درخواست کاربر (ارسال پیامک، خواندن پیامک، شماره گیری، پرس وجوی USSD، به روزرسانی وضعیت و …) در قالب یک object از نوع `Job` در صف قرار می گیرد و با callback مربوط تکمیل می شود. این فایل همچنین enumهای نوع و وضعیت Job و کلید شناسایی آن را تعریف می کند.

## ساختارهای کلیدی

### `JobId` و مقادیر enum
```cpp
using JobId                 = uint16_t;
constexpr JobId kInvalidJob = 0;

enum class JobKind : uint8_t
{
    None = 0,
    SmsSend, SmsRead, SmsScan, SmsDeleteIndex, SmsDeleteRead,
    CallDial, CallAnswer, CallHangup,
    StatusSweep,
    UssdQuery
};

enum class JobState : uint8_t
{
    Queued = 0, Running, Succeeded, Failed, Cancelled
};

const char* to_string(JobKind k);
const char* to_string(JobState s);
```

### `struct Job`
```cpp
struct Job
{
    JobId                                 id           = kInvalidJob;
    JobKind                               kind         = JobKind::None;
    JobState                              state        = JobState::Queued;
    bool                                  urgent       = false;
    // Set when a call interrupted the job: the retry must not consume attempts.
    bool                                  deferred     = false;
    uint8_t                               attempts     = 0;
    uint8_t                               max_attempts = 1;
    int16_t                               index        = -1;   // SMS storage index
    SmsEncoding                           encoding     = SmsEncoding::Auto;
    StaticString<GSM_SMS_NUMBER_BYTES>    number;
    StaticString<GSM_SMS_TEXT_BYTES>      text;
    StaticString<GSM_USSD_RESPONSE_BYTES> ussd_response;
    AtResult                              result;
    int16_t                               reference   = -1;    // SMS reference / etc.
    uint32_t                              enqueued_ms = 0;
    void                                  clear() { *this = Job{}; }
};

using JobCallback = Delegate<const Job&>;
```

فیلدهای مهم:
- **`urgent`**: اولویت بالا (مثلاً قطع تماس) — در پردازش صف مقدم است.
- **`deferred`** (ویژگی v0.4): وقتی تماس، Job را وقفه داده است، تلاش مجدد **شمارندهٔ attempts را مصرف نمی کند** تا Job به خاطر وقفهٔ بیرونی شکست نخورد.
- **`max_attempts`**: سقف تلاش مجدد؛ مرتبط با `GSM_JOB_START_RETRIES`.
- **`result`**: نتیجهٔ نهایی (`AtResult`)؛ برای callback و لاگ.
- **`enqueued_ms`**: زمان ورود به صف؛ مبنای `JobQueue::purge_stale`.

## جریان کاری

```text
Modem::send_sms(...)                          (public API)
    |
    v
Job job; fill kind/number/text/encoding       -> enqueue(job, cb)
    |
    v
JobQueue.push(job, cb)                        (FIFO, GSM_QUEUE_DEPTH slots)
    |
    v
Modem::tick(): pop next job when engine idle -> start_job()
    |                                          map JobKind -> IService (service_for)
    v
service runs steps (AtEngine) -> finish_job(AtResult)
    |
    v
job.state = Succeeded/Failed -> JobCallback(job)   (user notification)
```

نمونه:
```cpp
// Callback receives the finished job (id, result, payload).
JobCallback cb{&App::on_job_done, &app};
JobId id = modem.send_sms("+98912xxxxxxx", "hello", cb);
```

## نکات فنی

- **بدون تخصیص حافظه**: همهٔ رشته ها `StaticString` با اندازهٔ ثابت؛ خود `Job` در `JobQueue::Slot`های ایستا جا می شود.
- `Job` یک POD قابل بازنشانی با `clear()` است؛ اندازهٔ آن (به خاطر بافرها) در حد چند صد بایت است — `GSM_SMS_TEXT_BYTES` و `GSM_USSD_RESPONSE_BYTES` را متناسب حافظه تنظیم کنید.
- `JobCallback` از نوع `Delegate` است (تابع + context)، نه `std::function`؛ مناسب محیط های بدون RTTI/استثنا.
- `kInvalidJob = 0` یعنی شناسه ها از ۱ شروع می شوند (`Modem::next_id_ = 1`).
- `ussd_response` در Job نگهداری می شود تا نتیجهٔ USSD هم از مسیر همان callback استاندارد برگردد.

## ارتباط با سایر فایل ها

- `queue/job_queue.hpp` — صف نگهدارندهٔ Jobها
- `core/at_types.hpp` — `AtResult` در دل Job
- `sms/sms_types.hpp` — `SmsEncoding`
- `ussd/ussd_service.hpp` — اندازهٔ بافر `GSM_USSD_RESPONSE_BYTES`
- `util/delegate.hpp` — `JobCallback`
- `modem/modem.hpp` — سازندهٔ Jobها، `enqueue`/`finish_job` و APIهای عمومی
