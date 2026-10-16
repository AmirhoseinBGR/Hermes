# gsm/src/job_queue.cpp

## نقش فایل

پیاده‌سازی `gsm::JobQueue`؛ صف حلقوی ثابت‌اندازه برای jobهای مودم (ارسال پیامک، تماس، وضعیت، USSD) به‌همراه توابع متنی `JobKind` و `JobState`. این صف بدون هیچ تخصیص حافظه پویا کار می‌کند، از jobهای فوری (urgent) از سرِ صف پشتیبانی می‌کند، امکان لغو تکی/جمعی دارد، و jobهای کهنه را برای جلوگیری از انباشت (مثلاً وقتی مودم آفلاین است) پاک‌سازی می‌کند.

## توابع و منطق کلیدی

### `to_string(JobKind)` / `to_string(JobState)`
- `JobKind`: `"none" | "sms-send" | "sms-read" | "sms-scan" | "sms-del" | "sms-del-read" | "dial" | "answer" | "hangup" | "status" | "ussd"`
- `JobState`: `"queued" | "running" | "ok" | "failed" | "cancelled"`

### `bool JobQueue::push(const Job& job, JobCallback cb)`
افزودن به صف؛ اگر `job.urgent` باشد، job **سرِ صف** قرار می‌گیرد (شاخص `head_` یک خانه به عقب برده می‌شود) وگرنه به انتهای حلقه (`head_ + count_`) می‌رود:

```cpp
if (job.urgent) {
    head_ = (head_ + GSM_QUEUE_DEPTH - 1) % GSM_QUEUE_DEPTH;
    slots_[head_].job = job;
} else {
    const uint8_t tail = (head_ + count_) % GSM_QUEUE_DEPTH;
    slots_[tail].job   = job;
}
++count_;
```

صف پُر → `false` (فراخواننده یعنی `Modem::enqueue` با `kInvalidJob` پاسخ می‌دهد).

### `bool JobQueue::pop(Job& out, JobCallback& cb)`
برداشتن سرِ صف؛ اگر صف خالی یا **متوقف (`paused_`)** باشد `false`. اسلات خالی می‌شود (`job.clear()` و `cb.clear()`) تا به delegateهای نگهدارنده اشاره‌گر، منابع نشت نکند.

### `const Job* JobQueue::at(uint8_t i) const`
دسترسی فقط‌خواندنی به عنصر i‑ام از سرِ صف (برای نمایش در `Console::draw_queue`)؛ خارج از محدوده → `nullptr`.

### `bool JobQueue::cancel(JobId id, Job& out, JobCallback& cb)`
جست‌وجوی خطی در حلقه؛ پس از یافتن، job با وضعیت `Cancelled` بیرون داده می‌شود و اسلات‌ها با shift فشرده می‌شوند تا ترتیب حفظ شود.

### `void JobQueue::clear()` / `bool JobQueue::take_cancelled(...)`
`clear` همه jobها را با وضعیت `Cancelled` به آرایه `cancelled_[]` منتقل می‌کند (حداکثر `GSM_QUEUE_DEPTH` مورد). `Modem::flush_cancelled` سپس با `take_cancelled` (به‌صورت LIFO) هر کدام را بیرون کشیده و callback آن را صدا می‌زند تا هیچ درخواستی بدون پاسخ نماند.

### `uint8_t JobQueue::purge_stale(uint32_t now, uint32_t max_age_ms)`
حذف jobهایی که بیش از `max_age_ms` (در `Modem` مقدار `GSM_JOB_MAX_AGE_MS = 45000`) در صف مانده‌اند؛ حذف‌شده‌ها هم به `cancelled_[]` می‌روند تا callbackشان اجرا شود. تعداد حذف‌شده را برمی‌گرداند. `max_age_ms == 0` یعنی غیرفعال.

## جریان کاری

```text
push(job) ──► [ urgent؟ سرِ صف : انتهای صف ] ──► count++
                       │
Modem::tick ──► pop() ─┼─► paused_؟  (صبر)
                       ▼
                 start_job / اجرا
                       │
   cancel(id) ──► یافتن + shift فشرده‌سازی ──► callback با Cancelled
   clear()   ──► همه به cancelled_[] ──► take_cancelled → callback
   purge_stale(now, 45s) ──► jobهای کهنه به cancelled_[]
```

## نکات فنی

- **حلقه ثابت با `GSM_QUEUE_DEPTH = 6`:** همه اسلات‌ها از پیش تخصیص یافته‌اند؛ `head_` و `count_` شاخص‌های `uint8_t` با محاسبات modulo هستند — مناسب حافظه محدود و بدون قطعی (deterministic).
- **جابه‌جایی‌های O(n):** `cancel` و `purge_stale` با shift حلقه‌ای کار می‌کنند؛ با عمق ۶ ناچیز است.
- **jobهای urgent** (مثل `answer`/`hang_up` در `Modem`) حتی از jobهای جاریِ در صف هم جلوترند؛ اما job «در حال اجرا» هرگز وسط کار لغو نمی‌شود مگر با `Modem::cancel` یا `preempt_for_call`.
- **`paused_` فقط `pop` را می‌بندد؛** `push` همچنان کار می‌کند (می‌توان حین توقف، درخواست انباشت).
- **قرارداد callback:** هر jobی که از صف خارج می‌شود (موفق، لغو، purge) حتماً callback دارد؛ `clear` این را با بافر `cancelled_[]` تضمین می‌کند.
- **بدون heap:** `Slot` شامل `Job` + `JobCallback` (delegate ذخیره‌ای) است؛ هیچ `new`/`malloc` در مسیر بحرانی نیست.

## ارتباط با سایر فایل‌ها

- `gsm/queue/job_queue.hpp` — تعریف کلاس، `JobKind`، `JobState`.
- `gsm/queue/job.hpp` — ساختار `Job` (id، kind، attempts، enqueued_ms، number/text، نتیجه) و `JobCallback`.
- `gsm/util/delegate.hpp` — delegate بدون تخصیص حافظه برای callbackها.
- `src/modem.cpp` — مالک صف: `enqueue`/`pop`/`cancel`/`purge_stale`/`take_cancelled` و سیاست retry در `finish_job`.
- `include/gsm/gsm_config.hpp` — `GSM_QUEUE_DEPTH`، `GSM_JOB_MAX_AGE_MS`.
- `src/cli.cpp` — نمایش صف در پنل `q` (با `capacity()` و `queued(i)`).
