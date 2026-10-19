# gsm/ports/arduino/gsm_port_arduino.hpp

## نقش فایل

این فایل «پُرت آردوینو» (Arduino Port) برای کتابخانه ی GSM است. وظیفه اش این است که رابط های انتزاعی سخت افزاری (HAL) کتابخانه را روی محیط آردوینو پیاده سازی کند تا همان هسته ی AT-Engine بدون تغییر روی برد آردوینو کار کند. هر شیء `Stream` در آردوینو (مانند `HardwareSerial` یا `SoftwareSerial`) قابل استفاده است. این فایل فقط هدر است (کلاس ها `inline`/کوتاه تعریف شده اند) و نیاز به فایل `.cpp` جدا ندارد.

## ساختارها/توابع کلیدی

- `class ArduinoUart final : public hal::IUart`
  پیاده سازی رابط `IUart` روی یک `Stream&`. فقط یک عضو خصوصی `Stream& s_` نگه می دارد.
  - `explicit ArduinoUart(Stream& stream)` — سازنده، مرجع استریم سریال را می گیرد.
  - `uint16_t write(const uint8_t* data, uint16_t len)` — بازگرداندن `s_.write(data, len)` (تعداد بایت نوشته شده).
  - `uint16_t read(uint8_t* out, uint16_t max)` — حداکثر `max` بایت را یکی یکی از `s_.read()` می خواند تا زمانی که داده ای در دسترس نباشد؛ تعداد واقعی خوانده شده را برمی گرداند.
  - `uint16_t available()` — تعداد بایت های آماده در بافر دریافتی (`s_.available()`)، با محافظت در برابر مقدار منفی.
  - `void drain_rx()` — تمام بایت های باقی مانده در بافر دریافتی را می خواند و دور می ریزد (پاک سازی بافر RX).

- `class ArduinoClock final : public hal::IClock`
  پیاده سازی رابط زمان سنج.
  - `uint32_t millis()` — مقدار `::millis()` آردوینو را برمی گرداند (میلی ثانیه از شروع اجرا).

## جریان کاری

1. کاربر یک شیء `Stream` (مثلاً `Serial1`) می سازد و آن را به `ArduinoUart` می دهد.
2. `ArduinoUart` در نقش `IUart` به موتور AT کتابخانه داده می شود؛ موتور برای نوشتن دستورات از `write` و برای خواندن پاسخ ها از `read`/`available` استفاده می کند.
3. `ArduinoClock` زمان سنجی غیرمسدودکننده (non-blocking) موتور را تأمین می کند؛ چون `millis()` آردوینو سراسری است، نیازی به هیچ وضعیت (state) داخلی نیست.
4. `drain_rx` در زمان ریست/شروع مجدد برای دور ریختن باقی مانده های ناخواسته ی بافر به کار می رود.

نمونه استفاده (از فایل `examples/arduino_basic.ino`):

```cpp
port::ArduinoUart  modem_uart(Serial1);
port::ArduinoClock clk;
Modem              modem(modem_uart, clk, kSim800);
```

## نکات فنی

- تمام متدها `override` هستند و از رابط های `hal::IUart` / `hal::IClock` ارث می برند؛ بنابراین هسته ی کتابخانه به هیچ وابستگی آردوینویی نیاز ندارد.
- `write` مقدار بازگشتی `Stream::write` را به `uint16_t` کست (cast) می کند.
- `read` با حلقه ی `while` کار می کند نه یک فراخوانی؛ چون `Stream::read()` فقط یک بایت در هر نوبت برمی گرداند.
- `available` در برابر مقدار منفی `Stream::available()` محافظت شده و همیشه عدد غیرمنفی برمی گرداند.
- چون هیچ `new`/`malloc` یا استثنا (exception) به کار نرفته، این پُرت با پیکربندی `-fno-exceptions -fno-rtti` کتابخانه سازگار است.
- `ArduinoClock` عمداً هیچ وضعیتی نگه نمی دارد؛ زمان از تابع سراسری `millis()` خوانده می شود.

## ارتباط با سایر فایل ها

- `gsm/hal/hal.hpp` — تعریف رابط های `IUart` و `IClock` که این فایل آن ها را پیاده سازی می کند.
- `gsm/modem/modem.hpp` — کلاس `Modem` که نمونه ی `ArduinoUart`/`ArduinoClock` به آن پاس داده می شود.
- `gsm/examples/arduino_basic.ino` — نمونه ی کامل استفاده از این پُرت.
- `gsm/library.json` — پلتفرم `atmelavr`/`arduino` را به عنوان هدف اعلام می کند.
- در برابر `ports/host/gsm_port_host.hpp` (شبیه ساز میزبان) و `ports/stm32/gsm_port_stm32.hpp` (هسته ی HAL میکروکنترلر) قرار می گیرد؛ هر سه همان رابط HAL را روی سخت افزار متفاوت پیاده سازی می کنند.
