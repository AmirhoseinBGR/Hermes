# gsm/include/gsm/hal/hal.hpp

## نقش فایل

این فایل لایهٔ انتزاع سخت‌افزار (HAL) کتابخانه را تعریف می‌کند: رابط‌های خالص برای UART، زمان‌سنج و کنترل تغذیهٔ ماژول GSM. هدف، جداسازی کامل کد کتابخانه از پلتفرم مقصد است؛ کافی است کاربر پیاده‌سازی‌های متناسب با برد خود (STM32، Arduino، شبیه‌ساز تست و …) را بدهد تا همه‌جای کتابخانه بدون تغییر کار کند.

## ساختارهای کلیدی

### `class hal::IUart`
```cpp
class IUart
{
  public:
    virtual ~IUart()                                          = default;
    virtual uint16_t write(const uint8_t* data, uint16_t len) = 0;
    virtual uint16_t read(uint8_t* out, uint16_t max)         = 0;
    virtual uint16_t available()                              = 0;
    virtual void     drain_rx()                               = 0;
    virtual bool     set_baud(uint32_t) { return false; }
};
```
- `write` / `read`: انتقال بایت‌ها؛ تعداد واقعی خوانده/نوشته‌شده را برمی‌گردانند (غیرمسدودکننده).
- `available`: تعداد بایت آماده در بافر دریافت.
- `drain_rx`: خالی‌کردن بافر دریافت؛ پیش از شروع هر فرمان AT فراخوانی می‌شود تا باقیماندهٔ دادهٔ قبلی مخل نشود.
- `set_baud`: اختیاری؛ پیش‌فرض `false` (پشتیبانی نشده).

### `class hal::IClock`
```cpp
class IClock
{
  public:
    virtual ~IClock()         = default;
    virtual uint32_t millis() = 0;
};
```
منبع زمان مونوتونیک به میلی‌ثانیه با **سرریز uint32** (حدود ۴۹ روز). همهٔ تایمرهای کتابخانه بر همین مبنا کار می‌کنند.

### `class hal::IPowerControl`
```cpp
class IPowerControl
{
  public:
    virtual ~IPowerControl() = default;
    virtual void power_key(bool asserted) { (void)asserted; }
    virtual void reset(bool asserted) { (void)asserted; }
    virtual bool status() { return true; }
};
```
- `power_key`: پالس پین PWRKEY برای روشن/خاموش‌کردن ماژول.
- `reset`: پین ریست سخت‌افزاری.
- `status`: وضعیت روشن‌بودن ماژول (پیش‌فرض همیشه `true`).
همهٔ متدها دارای پیش‌فرض بی‌اثرند؛ در `Modem` این اینترفیس اختیاری است (`nullptr` یعنی بدون کنترل تغذیه).

### تابع کمکی `elapsed`
```cpp
inline bool elapsed(uint32_t now, uint32_t since, uint32_t span)
{ return static_cast<uint32_t>(now - since) >= span; }
```
محاسبهٔ سپری‌شدن زمان به‌صورت **مقاوم در برابر سرریز uint32**: تفریق ماژولار `now - since` حتی بعد از پیچیدن ساعت هم درست کار می‌کند. الگوی استاندارد همهٔ تایمرهای کتابخانه.

## جریان کاری

```text
   Application / Board support
   +--------------------------------------+
   | MyUart : IUart   MyClock : IClock    |
   | MyPower : IPowerControl (optional)   |
   +------------------+-------------------+
                      | references
                      v
   Modem(uart, clock, traits, power*)  -->  AtEngine(uart, clock, urc)
                      |
                      +--> hal::elapsed() used by all timers
```

نمونهٔ حداقلی استفاده:

```cpp
// Board provides implementations; the library only sees the interfaces.
gsm::hal::IUart& uart  = my_uart;
gsm::hal::IClock& clk  = my_clock;
gsm::Modem modem(uart, clk, gsm::kSim800, &my_power);
```

## نکات فنی

- **بدون تخصیص حافظه و بدون STL**: تنها `<cstdint>`؛ همه‌چیز reference/pointer و virtual dispatch.
- متدها **غیرمسدودکننده** طراحی شده‌اند — `read` نباید منتظر داده بماند؛ حلقهٔ اصلی با `Modem::tick()` داده را می‌کشد.
- `millis()` باید مونوتونیک باشد؛ از `elapsed()` برای مقایسه‌ها استفاده کنید نه مقایسهٔ مستقیم `now > since` (مقاومت در برابر سرریز).
- `drain_rx()` حیاتی است: باقیماندهٔ پاسخ فرمان قبلی در بافر، رفتار `AtEngine` را خراب می‌کند.
- اگر `IPowerControl` داده نشود، `Modem::has_power_control()` برمی‌گردد `false` و قابلیت‌های `pulse_pwrkey` / `power_cycle` عملاً غیرفعال‌اند.

## ارتباط با سایر فایل‌ها

- `core/at_engine.hpp` — `AtEngine` مستقیماً `IUart&` و `IClock&` می‌گیرد
- `modem/modem.hpp` — سازندهٔ `Modem` این اینترفیس‌ها را دریافت می‌کند (`IPowerControl*` اختیاری)
- `tools/serial_bridge.hpp` و `cli/cli.hpp` — از `IUart` به‌عنوان کنسول/پل سریال استفاده می‌کنند
- `core/line_reader.hpp` و `util/ring_buffer.hpp` — لایه‌های بافر بالای همین HAL
