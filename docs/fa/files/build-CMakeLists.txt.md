# gsm/CMakeLists.txt

## نقش فایل

پیکربندی ساخت (build) کتابخانه‌ی ایستای `gsm_modem` با CMake. این فایل فهرست فایل‌های منبع هسته را جمع می‌کند، کتابخانه‌ی ایستا (STATIC) می‌سازد، مسیرهای include را تنظیم می‌کند، گزینه‌های کامپایل (بدون استثنا/RTTI، هشدارها) را اعمال می‌کند و دو گزینه‌ی اختیاری برای پُرت STM32 و تست میزبان فراهم می‌کند.

## ساختارها/توابع کلیدی

- `cmake_minimum_required(VERSION 3.16)` و `project(gsm_modem CXX)` — حداقل نسخه‌ی CMake و نام پروژه (زبان C++).
- `set(CMAKE_CXX_STANDARD 17)` + `set(CMAKE_CXX_STANDARD_REQUIRED ON)` — استاندارد C++17 اجباری.
- `add_library(gsm_modem STATIC ...)` — ساخت کتابخانه‌ی ایستا از فایل‌های `src/`:
  `log.cpp`, `at_types.cpp`, `line_reader.cpp`, `urc_router.cpp`, `at_engine.cpp`, `ucs2.cpp`, `phone_number.cpp`, `sms_service.cpp`, `call_service.cpp`, `status_service.cpp`, `ussd_service.cpp`, `job_queue.cpp`, `watchdog.cpp`, `modem.cpp`, `cli.cpp`.
- `target_include_directories(gsm_modem PUBLIC include)` — پوشه‌ی `include/` به‌عنوان مسیر include عمومی.
- `target_compile_options(gsm_modem PRIVATE ...)` — هشدارها (`-Wall -Wextra -Wshadow`) و بهینه‌سازی حجم (`-ffunction-sections -fdata-sections`) به‌همراه `-fno-exceptions -fno-rtti`.
- `option(GSM_PORT_STM32 "Build the STM32 HAL port" OFF)` — اگر روشن باشد `ports/stm32` به include اضافه می‌شود.
- `option(GSM_BUILD_TESTS "Build the host smoke test" OFF)` — اگر روشن باشد:
  - `add_executable(host_smoke tests/host_smoke.cpp)`
  - `target_link_libraries(host_smoke PRIVATE gsm_modem)`
  - `target_include_directories(host_smoke PRIVATE ports/host)`
  - `enable_testing()` و `add_test(NAME host_smoke COMMAND host_smoke)`.

## جریان کاری

1. CMake پروژه را با C++17 تعریف می‌کند.
2. کتابخانه‌ی ایستای `gsm_modem` از پانزده فایل منبع هسته ساخته می‌شود؛ `include/` مسیر include عمومی است.
3. گزینه‌های کامپایل سخت‌گیرانه اعمال می‌شوند (بدون استثنا/RTTI، مناسب محیط‌های embedded).
4. در صورت نیاز، گزینه‌ی `GSM_PORT_STM32` مسیر پُرت STM32 را اضافه می‌کند.
5. در صورت نیاز، گزینه‌ی `GSM_BUILD_TESTS` اجرایی `host_smoke` را می‌سازد، به کتابخانه پیوند می‌دهد، پُرت میزبان را اضافه می‌کند و تست را با CTest ثبت می‌کند.

## نکات فنی

- کتابخانه STATIC است؛ برای مصرف در پروژه‌های embedded مناسب است.
- `-fno-exceptions -fno-rtti` با سیاست «بدون STL پویا/استثنا» سازگار است (همان‌طور که در `library.json` هم آمده).
- `-ffunction-sections -fdata-sections` به linker کمک می‌کند کد استفاده‌نشده را حذف کند (کاهش حجم فریمور).
- پرچم‌های کامپایل فقط `PRIVATE` هستند؛ مصرف‌کنندگان کتابخانه این پرچم‌ها را به ارث نمی‌برند.
- هر دو گزینه (`GSM_PORT_STM32`, `GSM_BUILD_TESTS`) پیش‌فرض `OFF` هستند تا ساخت پایه سبک بماند.
- `cli.cpp` (کنسول تعاملی v0.4) بخشی از هسته‌ی کتابخانه است و در `add_library` فهرست شده است.
- فهرست منبع دقیقاً با ساختار `src/` هم‌خوان است؛ هر سرویس (SMS، تماس، وضعیت، USSD، صف، واچ‌داگ) فایل مخصوص به خود را دارد.

## ارتباط با سایر فایل‌ها

- `gsm/src/*.cpp` — فایل‌های منبعی که در کتابخانه گنجانده شده‌اند.
- `gsm/include/` — سرصفحه‌های عمومی (مسیر include).
- `gsm/ports/stm32/` — با `GSM_PORT_STM32` اضافه می‌شود.
- `gsm/ports/host/` — برای تست میزبان اضافه می‌شود.
- `gsm/tests/host_smoke.cpp` — هدف تست میزبان.
- `gsm/library.json` — متادیتای PlatformIO؛ با این CMake هم‌راستا است (C++17، بدون استثنا/RTTI).
- `App/CMakeLists.txt` — لایه‌ی اپلیکیشن که `gsm_modem` را به‌عنوان وابستگی لینک می‌کند.
