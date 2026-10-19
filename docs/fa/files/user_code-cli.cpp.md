# gsm/user_code/cli.cpp

## نقش فایل

پیاده سازی «چسب سخت افزاری» (hardware glue) کنسول تعاملی روی STM32. این فایل فقط سخت افزار (UART مودم، UART دیباگ، زمان سنج، کنترل تغذیه) و نمونه ی مودم `kQuectelM08R` را می سازد و آن ها را به کنسول کتابخانه `gsm::cli::Console` وصل می کند. تمام منطق کنسول (منو، وضعیت زنده، دستورات، پاس ترو) در خود کتابخانه است؛ این فایل عمداً نازک نگه داشته شده است.

## ساختارها/توابع کلیدی

نمونه های ایستا (static) در سطح فایل:

- `static port::Stm32Uart<512> s_gsm_uart(&huart7)` — UART مودم (UART7) با بافر DMA ۵۱۲ بایتی.
- `static port::Stm32Uart<256> s_dbg_uart(&huart1)` — UART دیباگ/کنسول (USART1) با بافر DMA ۲۵۶ بایتی.
- `static port::Stm32Clock s_clk` — زمان سنج مبتنی بر `HAL_GetTick`.
- `static port::Stm32PowerControl s_gsmpwr(...)` — (شرطی، فقط اگر `GSM_PWR_GPIO_Port`/`GSM_PWR_Pin` تعریف شده باشند) کنترل PWRKEY مودم.
- `static Modem s_modem(s_gsm_uart, s_clk, kQuectelM08R, ...)` — نمونه ی مودم M08R؛ اگر پین تغذیه تعریف نشده باشد، `nullptr` برای کنترل تغذیه می گیرد.
- `static gsm::cli::Console s_console(s_modem, s_clk)` — کنسول کتابخانه متصل به مودم و زمان سنج.
- `static void uart_write(void*, const char* data, uint16_t len)` — متن خروجی کنسول را روی UART دیباگ می نویسد (به عنوان `TextSink`).

توابع عمومی `namespace cli`:

- `void init()`
  - `s_gsm_uart.begin()` و `s_dbg_uart.begin()` (شروع دریافت DMA).
  - تنظیم خروجی کنسول: `s_console.set_output(gsm::cli::TextSink{&uart_write, nullptr})`.
  - تنظیم فواصل نگهداری (housekeeping): `signal_ms=15000`، `sms_scan_ms=20000`، `full_sweep_ms=60000`، `cleanup_ms=300000`.
  - `s_modem.begin()` سپس `s_console.init()`.

- `void tick()`
  - `s_modem.tick()` و `s_console.tick()` را در هر چرخه صدا می زند.

- `void on_uart_rx(UART_HandleTypeDef* huart, uint16_t size)`
  - اگر `huart->Instance == UART7` → `s_gsm_uart.on_rx_event(size)` (داده ی مودم).
  - اگر `USART1` → `s_dbg_uart.on_rx_event(size)` سپس بایت ها یکی یکی با `s_console.feed(b)` به کنسول داده می شوند.

- `void on_uart_error(UART_HandleTypeDef* huart)`
  - خطای هر UART را به `on_error()` همان پُرت ارجاع می دهد تا دریافت بازیابی شود.

## جریان کاری

1. **راه اندازی:** `cli::init()` → شروع UARTها → تنظیم خروجی کنسول → تنظیم housekeeping → `modem.begin()` → `console.init()` (ثبت کال بک ها و رسم منو).
2. **حلقه ی اصلی:** `cli::tick()` → `modem.tick()` (پیشرفت ماشین حالت AT) + `console.tick()` (به روزرسانی نمایش/پاسخ).
3. **ورودی کاربر:** وقفه ی USART1 → `on_uart_rx` → `s_console.feed(b)` برای هر بایت → کنسول خط فرمان را کامل و اجرا می کند.
4. **خروجی:** کنسول متن را از طریق `uart_write` روی USART1 چاپ می کند.

## نکات فنی

- این فایل فقط «سیم کشی» است؛ هیچ منطق کنسول یا تجاری در آن نیست. منطق در `gsm::cli::Console` (کتابخانه) است.
- `s_gsm_uart` و `s_dbg_uart` اندازه ی DMA متفاوت (۵۱۲ / ۲۵۶) دارند؛ بسته به حجم ترافیک مودم در برابر کنسول.
- پین تغذیه با `#if defined(GSM_PWR_GPIO_Port) && defined(GSM_PWR_Pin)` اختیاری است؛ در نبود آن مودم بدون کنترل تغذیه ساخته می شود.
- `kQuectelM08R` نوع دستگاه (device trait) مودم را مشخص می کند؛ برخلاف مثال آردوینو که `kSim800` است.
- فواصل housekeeping (سیگنال، اسکن پیامک، جاروب کامل، پاک سازی) به میلی ثانیه و قابل تنظیم اند.
- `tick` باید مرتباً و بدون وقفه ی طولانی صدا زده شود تا هم مودم و هم کنسول پاسخ گو بمانند.

## ارتباط با سایر فایل ها

- `gsm/user_code/cli.hpp` — رابط عمومی که این فایل پیاده می کند.
- `gsm/include/gsm/cli/cli.hpp` — کلاس `gsm::cli::Console` (منطق کنسول).
- `gsm/ports/stm32/gsm_port_stm32.hpp` — `Stm32Uart`/`Stm32Clock`/`Stm32PowerControl`.
- `gsm/modem/modem.hpp` — کلاس `Modem` و ثابت `kQuectelM08R`.
- `gsm/examples/stm32_main.cpp` — نقطه ی ورودی که `cli::init`/`cli::tick`/`cli::on_uart_rx`/`cli::on_uart_error` را صدا می زند.
- `App/gsm_cli/cli.cpp` — نسخه ی مشابه برای اپلیکیشن کامل که افزون بر این ها پل وب (web bridge) هم دارد.
