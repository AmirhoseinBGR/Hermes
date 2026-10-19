# gsm/include/gsm/tools/serial_bridge.hpp

## نقش فایل

پل سریال (`SerialBridge`) برای اتصال یک کنسول بایت محور (پورت سریال، USB-CDC، تست هاس) به مودم GSM. در حالت **managed** دستورهای تایپ شده را خط به خط به موتور AT می دهد و پاسخ ها را echo می کند؛ در حالت **passthrough** بایت ها را مستقیم بین کنسول و UART مودم جابه جا می کند. ابزاری کاربردی برای دیباگ و کالیبراسیون بدون نوشتن کد میزبان.

## ساختارهای کلیدی

### `class SerialBridge final : public IAtObserver`
```cpp
SerialBridge(hal::IUart& console, Modem& modem);

void tick();                          // call in main loop
bool passthrough() const;
void set_passthrough(bool on);        // clears partial line on switch

// IAtObserver: echo engine output to the console
void on_line(const char* line, uint16_t len) override;
void on_final(const AtResult& res) override;
```

متدهای خصوصی:
```cpp
void echo(const char* text, uint16_t len);   // text + CRLF to console
void pump_managed();    // line editing + engine execution
void pump_passthrough(); // raw byte relay both directions (32-byte chunks)
```

فیلدها: `console_` (سریال کاربر)، `modem_`، `line_` (`StaticString<GSM_CMD_MAX>`)، `passthrough_`، `busy_`.

## جریان کاری

```text
                    tick()
                       |
       +---------------+----------------+
       | passthrough_  |                | managed
       v               |                v
console <--> modem UART|     console bytes -> line_ buffer
  (raw 32B chunks)     |        on CR/LF:
                       |          busy or engine busy -> echo("BUSY (engine)")
                       |          else -> modem.at().execute(line_, 10000, this)
                       |        0x1A -> at().write_ctrl_z()
                       |        0x1B -> at().write_escape()
                       |                |
                       |                v
                       |        on_line() -> echo to console
                       |        on_final() -> echo status name, busy_ = false
```

نمونهٔ استفاده:
```cpp
gsm::tools::SerialBridge bridge(console_uart, modem);
// main loop:
bridge.tick();
```

## نکات فنی

- **بدون تخصیص حافظه**: بافر خط `StaticString<GSM_CMD_MAX>` (پیش فرض ۲۵۶ بایت) و چانک های ۳۲ بایتی روی پشته.
- در حالت managed فقط **یک دستور در هر لحظه** اجرا می شود؛ اگر موتور مشغول باشد پیام `BUSY (engine)` چاپ می شود — دستور از دست نمی رود ولی باید دوباره تایپ شود.
- timeout هر دستور ۱۰ ثانیه ثابت است (مقدار `10000` در `execute`)؛ برای سناریوهای طولانی تر از `Modem`/CLI استفاده کنید.
- `write_ctrl_z` (‏`0x1A`) و `write_escape` (‏`0x1B`) برای پایان/انصراف payload پیامک در حالت prompt — رفتاری سازگار با ترمینال های AT.
- سوییچ `set_passthrough` بافر خط نیمه کاره را پاک می کند تا دستور ناقص به مودم نشت نکند.
- این کلاس `final` است و باید در حلقهٔ اصلی مرتباً `tick()` بخورد؛ خودش تایمر یا thread ندارد.
- شامل `modem/modem.hpp` است؛ پس وابستگی به کل پشتهٔ مودم دارد (ابزار سطح بالا، نه هسته).

## ارتباط با سایر فایل ها

- `modem/modem.hpp` — دسترسی به `modem.at()` (AtEngine) و وضعیت
- `core/at_engine.hpp` — `execute`/`write_ctrl_z`/`write_escape`
- `core/at_types.hpp` — `IAtObserver`، `to_string(AtStatus)`
- `hal/hal.hpp` — `IUart` برای هر دو سمت پل
- `cli/cli.hpp` — جایگزین غنی تر با منو و دستورات مدیریتی (پل سریال، ابزار مینیمال است)
- `gsm_config.hpp` — `GSM_CMD_MAX`
