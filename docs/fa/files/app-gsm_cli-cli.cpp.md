# App/gsm_cli/cli.cpp

## نقش فایل

چسب STM32 + پل وب (web bridge) برای کنسول کتابخانه ی GSM. این فایل سخت افزار (UART مودم، UART دیباگ، زمان سنج، کنترل تغذیه)، نمونه ی مودم `kQuectelM08R` و پل پنل وب را می سازد و همه را به کنسول کتابخانه `gsm::cli::Console` وصل می کند. خود کنسول یک قابلیت کتابخانه است (`gsm/cli/cli.{hpp,cpp}`)؛ این فایل فقط سیم کشی سخت افزار، نمونه ی مودم M08R و پل وب را انجام می دهد.

## ساختارها/توابع کلیدی

نمونه های ایستای سطح فایل:

- `static port::Stm32Uart<512> s_gsm_uart(&huart7)` — UART مودم (UART7، DMA ۵۱۲).
- `static port::Stm32Uart<256> s_dbg_uart(&huart1)` — UART دیباگ/کنسول (USART1، DMA ۲۵۶).
- `static port::Stm32Clock s_clk` — زمان سنج `HAL_GetTick`.
- `static port::Stm32PowerControl s_gsmpwr(...)` — (شرطی با `GSM_PWR_GPIO_Port`/`GSM_PWR_Pin`) کنترل PWRKEY.
- `static Modem s_modem(s_gsm_uart, s_clk, kQuectelM08R, ...)` — مودم M08R؛ در نبود پین تغذیه، `nullptr`.
- `static void uart_write(void*, const char* data, uint16_t len)` — خروجی کنسول روی UART دیباگ.
- `static gsm::cli::Console s_console(s_modem, s_clk)` — کنسول کتابخانه.

متغیر سراسری:

- `extern "C" bool g_sim_state` — وضعیت آماده بودن مودم/SIM برای مصرف بیرونی؛ در `tick` با `s_modem.ready()` به روز می شود.

توابع عمومی `namespace cli`:

- `void init()` — `s_gsm_uart.begin()`، `s_dbg_uart.begin()`، `s_console.set_output(...)`، تنظیم housekeeping (`signal_ms=15000`, `sms_scan_ms=20000`, `full_sweep_ms=60000`, `cleanup_ms=300000`)، سپس `s_modem.begin()` و `s_console.init()`.
- `void tick()` — اگر `s_console.passthrough()` باشد فقط `s_console.tick()` (پل خام مودم↔کنسول)؛ وگرنه `s_modem.tick()` + `s_console.tick()` و به روزرسانی `g_sim_state`.
- `void on_uart_rx(...)` — داده ی UART7 → `s_gsm_uart.on_rx_event`؛ USART1 → `s_dbg_uart.on_rx_event` + تغذیه ی بایت ها به `s_console.feed(b)`.
- `void on_uart_error(...)` — بازیابی دریافت هر UART با `on_error()`.
- `void set_web_output(void (*out)(const char*, void*), void* ud)` → `s_console.set_web_output(out, ud)`.
- `void exec_web(const char* line)` → `s_console.exec_web(line)`.
- `void status_line(char* dst, unsigned cap)` → `s_console.status_line(dst, cap)`.

## جریان کاری

1. **راه اندازی:** `cli::init()` → شروع UARTها → تنظیم خروجی کنسول → تنظیم housekeeping → `modem.begin()` → `console.init()`.
2. **حلقه:** `cli::tick()`:
   - در حالت **پاس ترو** (`passthrough()`): فقط `console.tick()` (پل خام بین مودم و کنسول).
   - در حالت عادی: `modem.tick()` + `console.tick()` + به روزرسانی `g_sim_state`.
3. **ورودی کاربر/دیباگ:** وقفه ی USART1 → `on_uart_rx` → `console.feed(b)` برای هر بایت.
4. **پل وب:** `set_web_output` مخاطب وب را ثبت می کند؛ `exec_web` دستور وب را اجرا می کند؛ `status_line` وضعیت فشرده ی زنده را می سازد.

## نکات فنی

- تفاوت کلیدی با `gsm/user_code/cli.cpp`: این نسخه **حالت پاس ترو** و **پل وب** دارد؛ در پاس ترو، `modem.tick()` صدا زده نمی شود و کنسول پل خام مودم↔کنسول است.
- `g_sim_state` با `extern "C"` در دسترس سایر بخش های C اپ (`App/port/drivers.cpp`) قرار می گیرد.
- منطق کنسول (منو، وضعیت زنده، دستورات، پاس ترو، وب) همگی در `gsm::cli::Console` است؛ این فایل فقط سیم کشی می کند.
- پین تغذیه با `#if defined(GSM_PWR_GPIO_Port) && defined(GSM_PWR_Pin)` اختیاری است.
- `kQuectelM08R` نوع دستگاه است (برخلاف `kSim800` در مثال آردوینو).
- توابع وب (`set_web_output`/`exec_web`/`status_line`) مستقیماً به متدهای هم نام `s_console` forwarding می شوند.

## ارتباط با سایر فایل ها

- `App/gsm_cli/cli.hpp` — رابط عمومی که این فایل پیاده می کند.
- `gsm/include/gsm/cli/cli.hpp` — `gsm::cli::Console` (منطق کنسول + متدهای وب).
- `gsm/ports/stm32/gsm_port_stm32.hpp` — `Stm32Uart`/`Stm32Clock`/`Stm32PowerControl`.
- `gsm/modem/modem.hpp` — کلاس `Modem` و `kQuectelM08R`.
- `App/port/drivers.cpp` — مصرف کننده ی پل وب و `g_sim_state`.
- `gsm/user_code/cli.cpp` — نسخه ی ساده تر (بدون پاس ترو/وب) برای نمونه ی کتابخانه.
