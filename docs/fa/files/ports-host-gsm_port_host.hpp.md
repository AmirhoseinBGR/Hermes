# gsm/ports/host/gsm_port_host.hpp

## نقش فایل

این فایل «پُرت میزبان» (Host Port) برای اجرای کتابخانه‌ی GSM روی کامپیوتر (بدون سخت‌افزار واقعی مودم) است. هدف اصلی‌اش تست و شبیه‌سازی است: به‌جای UART واقعی، بافرهای نرم‌افزاری می‌سازد که تست می‌تواند داده «تزریق» (inject) کند و فرستاده‌شده‌ها را بازرسی کند. زمان‌سنج و کنترل تغذیه هم به‌صورت مصنوعی/شمارنده‌ای پیاده شده‌اند تا تست‌ها قطعی (deterministic) باشند.

## ساختارها/توابع کلیدی

- `template <uint16_t Size = 2048> class HostUart final : public hal::IUart`
  UART شبیه‌سازی‌شده با بافر حلقه‌ای (`RingBuffer<Size>`) برای RX و یک آرایه‌ی `char` برای TX.
  - `uint16_t write(const uint8_t* data, uint16_t len)` — بایت‌ها را در بافر `tx_` می‌ریزد (به‌صورت رشته‌ی null-terminated) و `len` را برمی‌گرداند.
  - `uint16_t read(uint8_t* out, uint16_t max)` — تا `max` بایت از بافر RX (`rx_.pop`) برمی‌دارد.
  - `uint16_t available()` — تعداد بایت‌های موجود در بافر RX (`rx_.size()`).
  - `void drain_rx()` — بافر RX را خالی می‌کند (`rx_.clear()`).
  - `void inject(const char* text)` — رشته‌ای را به‌عنوان داده‌ی «دریافتی از مودم» وارد بافر RX می‌کند (برای شبیه‌سازی پاسخ مودم).
  - `const char* tx() const` — دسترسی به بافر ارسالی (برای بازرسی دستورات صادرشده توسط موتور).
  - `void clear_tx()` — بافر ارسالی را پاک می‌کند.

- `class HostClock final : public hal::IClock`
  زمان‌سنج مصنوعی با عضو عمومی `uint32_t now`.
  - `uint32_t millis()` — مقدار `now` را برمی‌گرداند؛ تست مقدار `now` را جلو می‌برد تا زمان را کنترل کند.

- `class HostPowerControl final : public hal::IPowerControl`
  شمارنده‌ی پالس تغذیه برای تشخیص ریست/پاورسایکل سخت‌افزاری در تست.
  - `void power_key(bool asserted)` — اگر `asserted` باشد `pulse_count` را یکی زیاد می‌کند.
  - `uint32_t pulse_count` — تعداد دفعات فشردن کلید تغذیه (برای اثبات اینکه watchdog سخت‌افزار را ریست کرده).

## جریان کاری

1. تست (`tests/host_smoke.cpp`) نمونه‌های `HostUart`، `HostClock` و `HostPowerControl` می‌سازد و به `Modem` می‌دهد.
2. تست با `advance(ms)` مقدار `clock_.now` را جلو می‌برد و `modem.tick()` را صدا می‌زند تا موتور AT پیش برود.
3. تست با `uart.inject("OK\r\n")` پاسخ‌های ساختگی مودم را وارد می‌کند و با `uart.tx()` / `tx_has(needle)` بررسی می‌کند که موتور چه دستوراتی فرستاده است.
4. `HostPowerControl.pulse_count` نشان می‌دهد که آیا watchdog در شرایط خطای متوالی، پاورسایکل سخت‌افزاری انجام داده است.

## نکات فنی

- `HostUart` یک قالب (template) با اندازه‌ی پیش‌فرض `2048` است؛ اندازه‌ی بافر RX و TX یکسان است.
- `write` همیشه `len` را برمی‌گرداند (حتی اگر بافر TX پر شود)؛ این برای سادگی تست است و بازگشت واقعی خروجی سریال را تضمین نمی‌کند.
- بافر TX به‌صورت رشته‌ی `'\0'`-terminated نگه داشته می‌شود تا بتوان با `strstr` (در `tx_has`) جست‌وجو کرد.
- `HostClock.now` عمومی است تا تست بتواند مستقیماً زمان را دستکاری کند (بدون وقفه‌ی واقعی).
- `HostPowerControl` فقط `power_key` را پیاده می‌کند؛ `reset` از پیش‌فرض رابط (`IPowerControl`) استفاده می‌کند.
- هیچ تخصیص پویا (dynamic allocation) یا استثنایی به کار نرفته؛ با `-fno-exceptions -fno-rtti` سازگار است.
- `std::strlen`/`std::memcpy` از `<cstring>` استفاده شده؛ برخلاف پُرت‌های embedded، اینجا STL مجاز است چون هدف تست میزبان است.

## ارتباط با سایر فایل‌ها

- `gsm/hal/hal.hpp` — رابط‌های `IUart`/`IClock`/`IPowerControl` که اینجا پیاده‌سازی می‌شوند.
- `gsm/util/ring_buffer.hpp` — `RingBuffer` که بافر RX از آن ساخته شده.
- `gsm/tests/host_smoke.cpp` — تنها مصرف‌کننده‌ی این پُرت؛ تمام سناریوهای تست (طول پیامک، نظارت تماس، watchdog، بازیابی SIM و…) از طریق همین شبیه‌ساز اجرا می‌شوند.
- `gsm/CMakeLists.txt` — با گزینه‌ی `GSM_BUILD_TESTS` پوشه‌ی `ports/host` را به include path اضافه می‌کند.
- در کنار `ports/arduino/` و `ports/stm32/` قرار می‌گیرد؛ هر سه یک رابط HAL را برای محیط متفاوت فراهم می‌کنند.
