# gsm/ports/stm32/gsm_port_stm32.hpp

## نقش فایل

این فایل «پُرت STM32» برای کتابخانه ی GSM است و رابط های انتزاعی سخت افزاری (HAL) را روی میکروکنترلرهای STM32 با کتابخانه ی HAL شرکت ST پیاده سازی می کند. سه بخش اصلی دارد: UART مبتنی بر DMA، زمان سنج مبتنی بر `HAL_GetTick`، و کنترل پین های تغذیه/ریست مودم (PWRKEY / RESET). این فایل به `main.h` (تولیدشده توسط CubeMX) وابسته است تا به هندل های UART و GPIO دسترسی داشته باشد.

## ساختارها/توابع کلیدی

- `template <uint16_t DmaSize = 256, uint16_t RingSize = GSM_RX_RING_SIZE> class Stm32Uart final : public hal::IUart`
  UART دریافتی مبتنی بر DMA با حلقه ی بافر (`RingBuffer<RingSize>`) برای تخلیه ی داده ها.
  - `explicit Stm32Uart(UART_HandleTypeDef* huart, uint32_t tx_timeout_ms = 100)` — سازنده با هندل UART و مهلت ارسال.
  - `void begin()` — دریافت DMA را شروع می کند (`start_rx`).
  - `UART_HandleTypeDef* handle() const` — دسترسی به هندل UART.
  - `uint16_t write(const uint8_t* data, uint16_t len)` — با `HAL_UART_Transmit` (Blocking با مهلت) ارسال می کند؛ در موفقیت `len` و در خطا `0`.
  - `uint16_t read(uint8_t* out, uint16_t max)` — تا `max` بایت از حلقه ی RX برمی دارد.
  - `uint16_t available()` — تعداد بایت های آماده در حلقه.
  - `void drain_rx()` — حلقه را خالی می کند.
  - `void on_rx_event(uint16_t size)` — کال بک رویداد RX (از `HAL_UARTEx_RxEventCallback`)؛ داده ی جدید DMA را از `last_pos_` تا `size` وارد حلقه می کند و حالت wrap-around بافر DMA را مدیریت می کند.
  - `void on_error()` — فلگ های خطای UART (ORE/NE/FE) را پاک می کند و دریافت را دوباره شروع می کند.
  - `void set_circular(bool on)` — حالت دریافت پیوسته ی حلقه ای DMA را روشن/خاموش می کند.

- `class Stm32Clock final : public hal::IClock`
  - `uint32_t millis()` — `HAL_GetTick()` را برمی گرداند (میلی ثانیه ی سراسری HAL).

- `class Stm32PowerControl final : public hal::IPowerControl`
  کنترل پین های تغذیه و ریست مودم با پشتیبانی از سطح فعال-پایین.
  - سازنده: `Stm32PowerControl(GPIO_TypeDef* pwr_port, uint16_t pwr_pin, GPIO_TypeDef* rst_port = nullptr, uint16_t rst_pin = 0, bool active_low = true)`
  - `void power_key(bool asserted)` — فقط سطح پین PWRKEY را ست/ریست می کند (بدون هیچ تأخیری). زمان بندی پولس (`assert` → نگه داشتن → `release` → boot guard) در `Modem::tick()` انجام می شود.
  - `void reset(bool asserted)` — فقط پین RESET را همان لحظه کنترل می کند (بدون تأخیر).
  - ⚠️ عمداً هیچ `HAL_Delay` در این کلاس نیست: اگر این توابع از داخل وقفه صدا زده شوند (مثل دستور کنسول که از ISR یو آرت می آمد)، `HAL_Delay` روی تیک SysTick قفل می شود و کل دستگاه هنگ می کند. `power_key()` را هرگز از ISR صدا نزنید.

## جریان کاری

1. **شروع دریافت:** `begin()` → `start_rx()` → `HAL_UARTEx_ReceiveToIdle_DMA` (با غیرفعال کردن وقفه ی نیمه بافر DMA).
2. **ورود داده:** وقفه ی DMA → `HAL_UARTEx_RxEventCallback` → `on_rx_event(size)` → انتقال بایت های جدید از بافر DMA به حلقه ی RX.
3. **مدیریت wrap:** اگر `size` از `last_pos_` کوچک تر باشد (نشانه ی چرخش بافر DMA)، داده از `last_pos_` تا انتها و سپس از ابتدای بافر DMA خوانده می شود.
4. **غیر حلقه ای:** اگر `circular_` خاموش باشد، پس از هر رویداد دریافت دوباره `start_rx()` صدا زده می شود.
5. **ارسال:** `write` از `HAL_UART_Transmit` Blocking استفاده می کند.
6. **تغذیه:** `Stm32PowerControl` پین های PWRKEY/RESET را برای روشن کردن یا ریست سخت افزاری مودم مدیریت می کند.

## نکات فنی

- `last_pos_` از نوع `volatile uint16_t` است چون در زمینه ی وقفه تغییر می کند.
- بافر DMA آرایه ی `uint8_t dma_[DmaSize]` با اندازه ی پیش فرض `256` است.
- `write` در صورت ورودی نال/طول صفر، `0` برمی گرداند؛ در خطا (غیر `HAL_OK`) نیز `0` برمی گرداند.
- تفکیک منطق فعال (`asserted ^ active_low_`) به پین های فعال-پایین یا فعال-بالا اجازه می دهد با یک سازنده سازگار شوند.
- `on_error` فلگ های Overrun/Noise/Framing را پاک می کند تا UART پس از خطا به حالت عادی برگردد.
- ماکروی `GSM_RX_RING_SIZE` باید در `main.h` یا پیکربندی پروژه تعریف شود.
- برخلاف پُرت میزبان، اینجا از هیچ STL پویایی استفاده نشده و با `-fno-exceptions -fno-rtti` سازگار است.

## ارتباط با سایر فایل ها

- `gsm/hal/hal.hpp` — رابط های `IUart`/`IClock`/`IPowerControl` که اینجا پیاده می شوند.
- `gsm/util/ring_buffer.hpp` — `RingBuffer` برای بافر RX.
- `main.h` (CubeMX) — تعریف `UART_HandleTypeDef`، `GPIO_TypeDef` و ماکروهای `GSM_RX_RING_SIZE`، `GSM_PWRKEY_PULSE_MS`.
- `gsm/user_code/cli.cpp` و `App/gsm_cli/cli.cpp` — نمونه های `Stm32Uart`/`Stm32Clock`/`Stm32PowerControl` را می سازند و به `Modem` می دهند.
- `gsm/examples/stm32_main.cpp` — نقطه ی ورود نمونه که کال بک های UART را به این پُرت وصل می کند.
- `gsm/CMakeLists.txt` — با گزینه ی `GSM_PORT_STM32` پوشه ی `ports/stm32` را به include اضافه می کند.
