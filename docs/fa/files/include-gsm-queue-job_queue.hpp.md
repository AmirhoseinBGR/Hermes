# gsm/include/gsm/queue/job_queue.hpp

## نقش فایل

صف FIFO ایستا و کم‌حجم برای Jobها با قابلیت لغو، تعلیق (pause) و پاک‌سازی Jobهای کهنه. این کلاس مکانیزم جلوگیری از انباشت و گیرافتادن صف را فراهم می‌کند (`purge_stale`) و فضای جداگانه‌ای برای Jobهای لغوشده دارد تا callback آن‌ها بی‌صدا حذف نشود.

## ساختارهای کلیدی

### `class JobQueue`
```cpp
class JobQueue
{
  public:
    bool       push(const Job& job, JobCallback cb);   // false if full/paused
    bool       pop(Job& out, JobCallback& cb);
    const Job* peek() const;                           // head without removing
    bool       cancel(JobId id, Job& out, JobCallback& cb);
    void       clear();
    bool       take_cancelled(Job& out, JobCallback& cb);

    // Drop jobs queued longer than max_age_ms (prevents accumulation/stuck queue)
    uint8_t purge_stale(uint32_t now, uint32_t max_age_ms);

    uint8_t                  size() const;
    bool                     empty() const;
    bool                     full() const;
    static constexpr uint8_t capacity() { return GSM_QUEUE_DEPTH; }
    const Job*               at(uint8_t i) const;      // inspection for CLI/debug

    void pause();
    void resume();
    bool paused() const;
};
```

ساختار داخلی:
```cpp
struct Slot
{
    Job         job;
    JobCallback cb;
};
Slot    slots_[GSM_QUEUE_DEPTH];      // FIFO ring
Slot    cancelled_[GSM_QUEUE_DEPTH];  // pending cancellation callbacks
uint8_t head_, count_, cancelled_count_;
bool    paused_;
```

متدهای کلیدی:
- `push`: درج در انتهای صف؛ اگر صف پر باشد `false`.
- `pop` / `peek`: برداشتن/دیدن سر صف.
- `cancel`: درآوردن Job با شناسهٔ مشخص و انتقال آن به بافر `cancelled_` تا `take_cancelled` (که `Modem::flush_cancelled` صدا می‌زند) callback لغو را اجرا کند.
- `purge_stale(now, max_age_ms)`: حذف Jobهایی که بیش از `GSM_JOB_MAX_AGE_MS` (پیش‌فرض ۴۵ ثانیه) در صف مانده‌اند؛ تعداد حذف‌شده را برمی‌گرداند.
- `at(i)`: دسترسی فقط‌خواندنی برای نمایش صف در CLI.

## جریان کاری

```text
Modem::enqueue()  --push-->  slots_[head ... head+count)
                                |
   tick(): purge_stale() -------+--> drop expired (>= GSM_JOB_MAX_AGE_MS)
                                |
   engine idle & !paused -------+
                                v
                        pop() -> current job -> start_job() -> service
                                |
   Modem::cancel(id) ----------> cancelled_[] -> flush_cancelled() -> cb(job)
```

## نکات فنی

- **بدون تخصیص حافظه**: آرایه‌های ایستای `GSM_QUEUE_DEPTH` (پیش‌فرض ۶) از `Slot`؛ هر Slot شامل کل Job (با بافرهایش) است — اندازهٔ حافظه را با `GSM_SMS_TEXT_BYTES`/`GSM_USSD_RESPONSE_BYTES` در نظر بگیرید.
- دو صف موازی `slots_` و `cancelled_` هم‌اندازه‌اند؛ حداکثر تعداد Jobهای لغوشدهٔ معلق = ظرفیت صف.
- **حالت pause**: پرچم `paused_` جریان برداشتن از صف را کنترل می‌کند؛ API سطح بالاتر آن `Modem::pause_queue`/`resume_queue` است (برای مثال هنگام تعلیق عمدی سرویس‌دهی).
- `purge_stale` با الگوی `hal::elapsed` (تفریق ماژولار `uint32`) در برابر سرریز ساعت مقاوم است.
- FIFO ساده است؛ اولویت‌دهی `urgent` در لایهٔ `Modem` انجام می‌شود نه در این کلاس.
- callback لغو عمداً از مسیر `take_cancelled` جدا شده تا در میانهٔ پردازش صف، فراخوانی callback باعث بازگشت بازگشتی (reentrancy) نشود.

## ارتباط با سایر فایل‌ها

- `queue/job.hpp` — نوع `Job`، `JobId`، `JobCallback`
- `gsm_config.hpp` — `GSM_QUEUE_DEPTH`، `GSM_JOB_MAX_AGE_MS`، `GSM_JOB_START_RETRIES`
- `modem/modem.hpp` — مالک `queue_`؛ `enqueue`/`flush_cancelled`/`stop_all`/`cancel`
- `cli/cli.hpp` — نمایش صف با `queued(i)`/`queue_size()`
