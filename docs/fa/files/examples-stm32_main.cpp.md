# gsm/examples/stm32_main.cpp

## نقش فایل

نقطه ی ورود (entry point) نمونه ی اپلیکیشن STM32. این فایل عمداً حداقلی است: منطق اپ را کوتاه نگه می دارد و آزمون تعاملی را به ماژول `cli` (چسب سخت افزاری کنسول کتابخانه) واگذار می کند. همچنین کال بک های رویداد DMA و خطای UART ساخته شده توسط CubeMX را به ماژول `cli` پل می زند.

## ساختارها/توابع کلیدی

همه به صورت `extern "C"` تعریف شده اند تا با چارچوب فراخوانی C سازگار باشند:

- `int AppInit()`
  - `cli::init()` را صدا می زند (راه اندازی UARTها، مودم، کنسول) و `0` برمی گرداند.

- `int AppRun()`
  - `cli::tick()` را صدا می زند (یک تیک از مودم + کنسول) و `0` برمی گرداند. این تابع در حلقه ی اصلی فراخوانی می شود.

- `void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t size)`
  - کال بک رویداد دریافت DMA (از HAL) → `cli::on_uart_rx(huart, size)`.

- `void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart)`
  - کال بک خطای UART (از HAL) → `cli::on_uart_error(huart)`.

## جریان کاری

1. سیستم/بوت لودر پروژه، `AppInit()` را یک بار صدا می زند → `cli::init()` سخت افزار و مودم را آماده می کند.
2. در حلقه ی اصلی، `AppRun()` مرتباً صدا زده می شود → `cli::tick()` مودم و کنسول را جلو می برد.
3. هنگام رسیدن داده به UART (مودم یا دیباگ)، HAL وقفه را مدیریت کرده و `HAL_UARTEx_RxEventCallback` را صدا می زند → `cli::on_uart_rx` داده را به پُرت/کنسول هدایت می کند.
4. در صورت خطای UART، `HAL_UART_ErrorCallback` → `cli::on_uart_error` دریافت را بازیابی می کند.

## نکات فنی

- این فایل هیچ منطق تجاری/کنسول ندارد؛ همه چیز به ماژول `cli` و کتابخانه `gsm_modem` تفویض شده است.
- توابع `extern "C"` برای این است که CubeMX/HAL بتواند مستقیماً آن ها را به عنوان کال بک صدا بزند (بدون name mangling).
- `AppInit`/`AppRun` قرارداد نام گذاری چارچوب اپ (`app.hpp`) هستند؛ مقدار بازگشتی `0` به معنای موفقیت است.
- `HAL_UARTEx_RxEventCallback` مربوط به دریافت «تا بی کار» (Receive-To-Idle) با DMA است؛ همان مکانیزمی که `Stm32Uart::on_rx_event` انتظار دارد.
- تفکیک UART مودم (UART7) در برابر UART دیباگ (USART1) داخل `cli.cpp` انجام می شود، نه اینجا.

## ارتباط با سایر فایل ها

- `gsm/user_code/cli.hpp` / `gsm/user_code/cli.cpp` — ماژول `cli` که این فایل آن را صدا می زند.
- `app.hpp` — چارچوب اپلیکیشن که قرارداد `AppInit`/`AppRun` را تعریف می کند.
- `usart.h` (CubeMX) — تعریف هندل های UART و کال بک های HAL.
- `gsm/ports/stm32/gsm_port_stm32.hpp` — پُرت UART/Clock/Power که به صورت غیرمستقیم از طریق `cli.cpp` استفاده می شود.
- `App/gsm_cli/cli.cpp` — نسخه ی اپلیکیشن کامل تر همین الگو (با پل وب).
