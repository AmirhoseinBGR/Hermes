# فهرست مستندات فایل‌ها (فارسی)

جدول زیر همه‌ی فایل‌های مستندشده در `gsm/docs/fa/files/` را فهرست می‌کند. نام فایل مستند از مسیر نسبی فایل منبع (جداشده با `-`) ساخته شده است.

| مسیر فایل | خلاصه |
|---|---|
| `gsm/ports/arduino/gsm_port_arduino.hpp` | پُرت آردوینو: پیاده‌سازی `IUart`/`IClock` روی هر `Stream` سریال و `millis()` آردوینو. |
| `gsm/ports/host/gsm_port_host.hpp` | پُرت میزبان: شبیه‌ساز UART/Clock/Power با بافر تزریق/بازرسی برای تست‌های قطعی. |
| `gsm/ports/stm32/gsm_port_stm32.hpp` | پُرت STM32: UART مبتنی بر DMA، زمان‌سنج `HAL_GetTick` و کنترل PWRKEY/RESET مودم. |
| `gsm/user_code/cli.hpp` | هدر چسب نازک STM32 برای کنسول کتابخانه (مقداردهی، تیک، کال‌بک‌های UART). |
| `gsm/user_code/cli.cpp` | پیاده‌سازی چسب STM32: نمونه‌های UART/Clock/Power + مودم M08R + کنسول کتابخانه. |
| `gsm/examples/arduino_basic.ino` | نمونه‌ی مینیمال آردوینو: راه‌اندازی مودم SIM800، چاپ پیامک دریافتی و ارسال یک پیامک فارسی. |
| `gsm/examples/stm32_main.cpp` | نقطه‌ی ورود نمونه‌ی STM32: فراخوانی `AppInit`/`AppRun` و پل کال‌بک‌های HAL به ماژول `cli`. |
| `gsm/tests/host_smoke.cpp` | تست دود میزبان: کدک، بوت، USSD، نگهبان طول پیامک، نظارت تماس، صف، بازیابی SIM، واچ‌داگ. |
| `gsm/CMakeLists.txt` | پیکربندی CMake کتابخانه‌ی ایستای `gsm_modem` + گزینه‌های پُرت STM32 و تست میزبان. |
| `gsm/library.json` | متادیتای PlatformIO: نام/نسخه/توضیحات، پلتفرم‌ها/چارچوب‌ها و پرچم‌های ساخت C++17. |
| `App/gsm_cli/cli.hpp` | هدر چسب اپلیکیشن: کنسول کتابخانه + رابط پل وب/سریال (آینه‌ی لاگ، اجرای دستور، وضعیت زنده). |
| `App/gsm_cli/cli.cpp` | چسب STM32 + پل وب: سخت‌افزار، مودم M08R، حالت پاس‌ترو و forwarding متدهای وب به کنسول. |
| `App/CMakeLists.txt` | پیکربندی CMake لایه‌ی اپلیکیشن `app` (OBJECT) با وابستگی به `gsm_modem` و تنظیم سطح لاگ. |
