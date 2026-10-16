# gsm/include/gsm/core/line_reader.hpp

## نقش فایل

تبدیل جریان بایت‌های دریافتی از UART به واحدهای معنادار پروتکل AT: یک **خط کامل** (با `\r` یا `\n` جدا شده) یا نشان **prompt** (کاراکتر `>` که مودم برای دریافت payload پیامک/USSD می‌فرستد). این کلاس بافر جمع‌آوری خطوط `AtEngine` است.

## ساختارهای کلیدی

### `enum class Token : uint8_t`
```cpp
enum class Token : uint8_t
{
    None,      // byte consumed, nothing complete yet
    Line,      // a full line is ready in line()
    Prompt,    // '>' prompt seen (payload mode)
    Overflow   // line exceeded GSM_LINE_MAX; buffer was reset
};
```

### `class LineReader`
```cpp
class LineReader
{
  public:
    Token       feed(uint8_t byte);               // consume one byte
    const char* line() const { return buf_.c_str(); }
    uint16_t    length() const { return buf_.size(); }
    void        reset() { buf_.clear(); }

  private:
    StaticString<GSM_LINE_MAX> buf_;
};
```
- `feed`: به‌ازای هر بایت یک `Token` برمی‌گرداند؛ با دریافت terminator خط، محتوای `line()` معتبر است.
- `line()` / `length()`: محتوای خط جاری/تکمیل‌شده (رشتهٔ null-terminated).
- `reset`: پاک‌سازی بافر (پس از مصرف خط یا در شروع تراکنش جدید).
- `Overflow`: خط از `GSM_LINE_MAX` (پیش‌فرض ۵۱۲) بلندتر شده؛ بافر ریست می‌شود تا خط خراب به تراکنش نشت نکند.

## جریان کاری

```text
uart.read() bytes
      |
      v
LineReader.feed(byte) --------------+
      |                             |
      +-> Token::Line   -> UrcRouter.dispatch(line) or observer->on_line(line)
      +-> Token::Prompt -> observer->on_prompt()      (expect_prompt)
      +-> Token::Overflow -> drop the broken line
      +-> Token::None   -> keep feeding
```

مثال مصرف (در `AtEngine::tick`):

```cpp
// Pseudocode inside AtEngine::tick()
switch (reader_.feed(b)) {
    case Token::Line:   handle_line(reader_.line(), reader_.length()); break;
    case Token::Prompt: if (expect_prompt_) observer_->on_prompt();    break;
    case Token::Overflow: /* drop corrupt line */                      break;
    case Token::None:   break;
}
```

## نکات فنی

- **بدون تخصیص حافظه**: بافر از نوع `StaticString<GSM_LINE_MAX>` و روی خود object قرار دارد؛ اندازه با ماکروی `GSM_LINE_MAX` از `gsm_config.hpp` کنترل می‌شود.
- **بدون STL**: فقط `<cstdint>` و `StaticString`.
- overflow آشکارسازی می‌شود (`Token::Overflow`)؛ خطوط خراب بی‌صدا حذف می‌شوند نه اینکه نصفه تحویل شوند.
- تفکیک URC از پاسخ فرمان بر عهدهٔ این کلاس نیست؛ `LineReader` فقط قاب‌بندی خط را انجام می‌دهد و `AtEngine` تصمیم می‌گیرد خط به `UrcRouter` برود یا به observer.
- `>` بدون terminator به‌عنوان prompt شناخته می‌شود (برای حالت `+CMGS` / `+CUSD`).

## ارتباط با سایر فایل‌ها

- `core/at_engine.hpp` — مالک یک `LineReader` به‌نام `reader_`
- `util/static_string.hpp` — بافر زیرساختی
- `gsm_config.hpp` — `GSM_LINE_MAX`
- `core/urc_router.hpp` — مصرف‌کنندهٔ خطوط کامل خروجی
