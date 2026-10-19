# gsm/include/gsm/util/ring_buffer.hpp

## نقش فایل

بافر حلقوی (FIFO) بایت محور با اندازهٔ ثابت و بدون heap، مناسب بافر دریافت UART در سطح راه انداز یا هر جریان بایتی دیگر. سر و ته بافر `volatile` هستند تا در حالت های وقفه ای (ISR تولیدکننده / حلقهٔ اصلی مصرف کننده) قابل استفاده باشد؛ overflow به صورت صریح علامت گذاری می شود.

## ساختارهای کلیدی

### `template <uint16_t N> class RingBuffer`
```cpp
template <uint16_t N> class RingBuffer
{
  public:
    bool     push(uint8_t b);                       // false when full
    uint16_t push(const uint8_t* d, uint16_t n);    // returns bytes accepted
    bool     pop(uint8_t& out);                     // false when empty
    uint16_t size() const;
    bool     empty() const;
    void     clear();
    bool     overflowed() const;
    void     clear_overflow();

  private:
    static uint16_t   advance(uint16_t i) { return (i + 1) % N; }
    uint8_t           buf_[N]{};
    volatile uint16_t head_ = 0;   // write index
    volatile uint16_t tail_ = 0;   // read index
    volatile bool     overflow_ = false;
};
```

- `push(byte)`: درج در `head_`؛ اگر `advance(head_) == tail_` باشد بافر پر است → `false` و `overflow_ = true` (بایت جدید **دور انداخته می شود**، بافر بازنویسی نمی شود).
- `push(data, n)`: حداکثر `n` بایت را قبول می کند و تعداد پذیرفته شده را برمی گرداند (توقف در اولین پرشدگی).
- `pop(out)`: برداشت یک بایت از `tail_`؛ خالی → `false`.
- `size()`: تعداد بایت های موجود (مقاوم در برابر wrap).
- `overflowed()` / `clear_overflow()`: پرچم overflow برای تشخیص از دست رفتن داده در سطح بالاتر.

## جریان کاری

```text
ISR / producer                         main loop / consumer
-----------------                      --------------------
ring.push(byte)                        while (ring.pop(b)) process(b);
   |                                        |
   +-- full? -> overflow_=true, drop        +-- empty -> pop returns false

clear() : tail_ = head_ (drop all), reset overflow flag
```

نمونه:
```cpp
// UART RX ring sized at compile time.
gsm::RingBuffer<1024> rx;
// ISR side:
rx.push(byte);
// loop side:
uint8_t b;
while (rx.pop(b)) { /* feed LineReader / engine */ }
```

## نکات فنی

- **بدون heap / بدون STL / بدون قفل**: آرایهٔ ایستا و ایندکس های `volatile`; امن برای یک تولیدکننده/یک مصرف کننده (SPSC) بدون mutex.
- ظرفیت واقعی `N - 1` بایت است (یک خانه برای تفکیک full از empty رزرو شده).
- overflow داده را از دست می دهد ولی ساختار را خراب نمی کند؛ پرچم آن برای لاگ/تشخیص افت بسته ضروری است.
- درصد باقیماندهٔ `(i + 1) % N` اگر `N` توان ۲ باشد به mask bit ارتقا می یابد (کامپایلر بهینه می کند)؛ `N` دلخواه هم مجاز است.
- توابع `push`/`pop` کوتاه و قابل inline شدن اند؛ مناسب ISR.
- این بافر بایت است و مفاهیم خط/فرمان ندارد — لایهٔ `LineReader` بالای آن (یا مستقیم روی UART) کار قاب بندی را می کند.
- `head_`/`tail_` فقط در یک سمت نوشته می شوند (هر کدام توسط یک نقش)، بنابراین الگوی SPSC بدون hazard است؛ برای چندمصرف کننده طراحی نشده.

## ارتباط با سایر فایل ها

- `hal/hal.hpp` — `IUart::read/available` اغلب با همین بافر در راه انداز برد پیاده می شود
- `gsm_config.hpp` — `GSM_RX_RING_SIZE` (اندازهٔ معمول بافر دریافت)
- `util/static_string.hpp` — مکمل این بافر برای ذخیرهٔ رشته ای
- `core/line_reader.hpp` — لایهٔ قاب بندی خط روی جریان بایتی
