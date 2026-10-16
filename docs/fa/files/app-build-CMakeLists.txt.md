# App/CMakeLists.txt

## نقش فایل

پیکربندی ساخت لایه‌ی اپلیکیشن «EvoHatch hub». این فایل کتابخانه‌ی `app` را به‌صورت OBJECT می‌سازد، منطق هسته (core، بدون HAL)، چسب STM32/LVGL (port)، ماژول هشدار (alarm) و ماژول CLI مودم GSM (`gsm_cli/cli.cpp`) را گرد هم می‌آورد، و به کتابخانه‌های وابسته (`def`, `etl`, `eeprom`, `user_ui`, `gsm_modem`) پیوند می‌دهد. سطح لاگ اپلیکیشن را هم تنظیم می‌کند.

## ساختارها/توابع کلیدی

- هدر توضیحی: ساختار پوشه‌ها را شرح می‌دهد:
  - `core/` — منطق بدون HAL (تست‌شده با `make -C tests run`).
  - `port/` — چسب STM32 + LVGL + user_ui.
  - `alarm/` — هوک خطا (نمونه‌ی کامل یا قالب خالی).

- `option(APP_ALARM_EXAMPLE "Build the worked alarm example instead of the empty template" OFF)`
  انتخاب بین نمونه‌ی کامل هشدار و قالب خالی.

- انتخاب منبع alarm:
  - اگر `APP_ALARM_EXAMPLE` روشن → `alarm/alarm_actions.c`.
  - وگرنه → `alarm/alarm_actions_template.c`.

- `add_library(app OBJECT ...)` — کتابخانه‌ی OBJECT از فایل‌ها:
  - `core/device.c`, `core/errors.c`, `core/rs485.c`, `core/esp_link.c`, `core/settings_store.c`
  - `port/app_main.c`, `port/board.c`, `port/ui_bridge.c`, `port/esp_sync.c`, `port/drivers.cpp`
  - `${APP_ALARM_SRC}` (بسته به گزینه)
  - `gsm_cli/cli.cpp`

- `target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})` — ریشه‌ی پوشه‌ی App مسیر include عمومی.

- `target_link_libraries(app PRIVATE def etl eeprom user_ui gsm_modem)` — پیوند به کتابخانه‌های وابسته، از جمله `gsm_modem` (کتابخانه‌ی GSM).

- `target_compile_definitions(app PRIVATE APP_LOG_LEVEL=APP_LOG_WARN)` — سطح لاگ اپ (پیش‌فرض WARN؛ گزینه‌های ERROR/WARN/INFO/TRACE که TRACE هر تراکنش گذرگاه را چاپ می‌کند).

## جریان کاری

1. CMake گزینه‌ی `APP_ALARM_EXAMPLE` را بررسی می‌کند و فایل منبع alarm مناسب را انتخاب می‌کند.
2. کتابخانه‌ی OBJECT به نام `app` از فایل‌های core، port، alarm و `gsm_cli/cli.cpp` ساخته می‌شود.
3. مسیر ریشه‌ی App به‌عنوان include عمومی اضافه می‌شود (دسترسی به `core/`, `port/`, `gsm_cli/` و…).
4. کتابخانه به `def`, `etl`, `eeprom`, `user_ui` و `gsm_modem` پیوند می‌خورد؛ `gsm_modem` همان کتابخانه‌ی GSM است.
5. سطح لاگ با `APP_LOG_LEVEL=APP_LOG_WARN` تعریف می‌شود.

## نکات فنی

- `add_library(app OBJECT ...)` یعنی کتابخانه‌ی اشیاء (object files)؛ این فایل‌ها جداگانه آبجکت می‌شوند و در نهایت به اجرایی نهایی پیوند می‌خورند.
- `gsm_cli/cli.cpp` (چسب کنسول مودم + پل وب) بخشی از همین کتابخانه‌ی app است؛ منطق کنسول خود در `gsm_modem` قرار دارد.
- `APP_LOG_LEVEL` یک ماکروی کامپایل است؛ سطح `APP_LOG_WARN` پیش‌فرض است و `APP_LOG_TRACE` برای اشکال‌زدایی هر تراکنش گذرگاه (bus) به کار می‌رود.
- گزینه‌ی `APP_ALARM_EXAMPLE` پیش‌فرض `OFF` است؛ یعنی قالب خالی هشدار ساخته می‌شود مگر صراحتاً روشن شود.
- پیوندها `PRIVATE` هستند؛ جزئیات کتابخانه‌های وابسته به مصرف‌کنندگان app درز نمی‌کند.
- `port/drivers.cpp` (پل وب/سریال) در همین کتابخانه است و از `gsm_cli/cli.cpp` (یعنی `set_web_output`/`exec_web`/`status_line`) استفاده می‌کند.

## ارتباط با سایر فایل‌ها

- `App/gsm_cli/cli.cpp` / `App/gsm_cli/cli.hpp` — چسب کنسول مودم + پل وب که در این کتابخانه گنجانده شده.
- `gsm/CMakeLists.txt` — کتابخانه‌ی `gsm_modem` که اینجا لینک می‌شود.
- `App/port/drivers.cpp` — پل وب/سریال (مصرف‌کننده‌ی API وب کنسول).
- `App/core/*` — منطق بدون HAL اپلیکیشن.
- `App/backend/eeprom/` و `App/backend/user_ui/` — کتابخانه‌های `eeprom` و `user_ui` که لینک می‌شوند.
- کتابخانه‌های `def`, `etl` — وابستگی‌های عمومی ابزار/کانتینر.
