# App/gsm_cli/cli.hpp

## نقش فایل

هدر «چسب» (glue) اپلیکیشن برای کنسول کتابخانه ی GSM روی STM32، به همراه پل وب/سریال (web/serial bridge). مانند `gsm/user_code/cli.hpp`، این فایل رابط نازکی برای مقداردهی، تیک، و کال بک های UART فراهم می کند؛ با این تفاوت که افزون بر آن، امکان آینه کردن لاگ/رویدادها به یک مصرف کننده ی بیرونی (کنسول وب) و اجرای دستور از پل وب را هم اعلام می کند. تمام منطق کنسول در `gsm::cli::Console` (کتابخانه) است.

## ساختارها/توابع کلیدی

همه در `namespace cli`:

- `void init()`
  مقداردهی اولیه ی زیرسیستم CLI (سخت افزار، مودم، کال بک ها).

- `void tick()`
  پردازش یک تیک CLI (از ححلقه ی اصلی).

- `void on_uart_rx(UART_HandleTypeDef* huart, uint16_t size)`
  کال بک رویداد دریافت UART (HAL)؛ داده ی مودم و دیباگ را به کنسول هدایت می کند.

- `void on_uart_error(UART_HandleTypeDef* huart)`
  کال بک خطای UART (HAL)؛ برای بازیابی دریافت.

- `void set_web_output(void (*out)(const char* line, void* ud), void* ud)`
  آینه کردن لاگ های مودم و رویدادها به یک مصرف کننده ی بیرونی (کنسول وب).

- `void exec_web(const char* line)`
  اجرای یک دستور از پل وب/سریال؛ متن پاسخ به صورت خط به خط به خروجی وب ثبت شده می رود (ANSI در ادامه ی مسیر حذف می شود).

- `void status_line(char* dst, unsigned cap)`
  ساخت محموله ی فشرده ی وضعیت زنده برای پنل وب (قالب CSV).
  قالب: `fsm|act|step|ready|sim|net|op|rssi|bars|smsu|smst|paused|qsize|qcap|call|peer`

## جریان کاری

1. نقطه ی ورود اپلیکیشن `cli::init()` را صدا می زند؛ در هر ححلقه `cli::tick()`.
2. وقفه های UART به `cli::on_uart_rx`/`cli::on_uart_error` وصل می شوند.
3. پل وب: `set_web_output` یک مخاطب (consumer) برای لاگ/رویدادها ثبت می کند؛ `exec_web` دستورات ورودی وب را اجرا می کند؛ `status_line` وضعیت فشرده ی زنده را برای پنل وب می سازد.
4. منطق واقعی کنسول در `gsm::cli::Console` انجام می شود؛ این فایل فقط API سیم کشی را در معرض قرار می دهد.

## نکات فنی

- این نسخه از `user_code/cli.hpp` کامل تر است: افزون بر کنسول سریال، پل وب (وب بریج) هم دارد.
- `status_line` یک رشته ی CSV فشرده می سازد که فیلدهایش با `|` جدا شده اند؛ برای مصرف پنل وب طراحی شده.
- `exec_web` پاسخ را «خط به خط» می دهد و «ANSI در ادامه ی مسیر حذف می شود»؛ یعنی رنگ های ترمینال به وب منتقل نمی شوند.
- `UART_HandleTypeDef*` به `main.h` (CubeMX) وابسته است.
- توابع نازک اند؛ هیچ منطق تجاری/کنسول در این هدر نیست.

## ارتباط با سایر فایل ها

- `App/gsm_cli/cli.cpp` — پیاده سازی این رابط (سخت افزار + کنسول + پل وب).
- `gsm/include/gsm/cli/cli.hpp` — `gsm::cli::Console` که `set_web_output`/`exec_web`/`status_line` را پیاده می کند.
- `App/port/drivers.cpp` — مصرف کننده ی پل وب (طبق توضیح هدر).
- `gsm/ports/stm32/gsm_port_stm32.hpp` — پُرت UART/Clock/Power.
- `gsm/user_code/cli.hpp` — نسخه ی ساده تر (بدون پل وب) برای نمونه ی کتابخانه.
