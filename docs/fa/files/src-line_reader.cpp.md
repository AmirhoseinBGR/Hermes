# gsm/src/line_reader.cpp

## نقش فایل

پیاده سازی `gsm::LineReader`؛ توکنایزر بایت به بایت جریان UART مودم که بایت های خام را به سه نوع توکن تبدیل می کند: خط کامل (`Token::Line`)، پرامپت ورود داده (`Token::Prompt`، یعنی `>` معروف `AT+CMGS`) و سرریز بافر (`Token::Overflow`). این کوچک ترین فایل کتابخانه است اما پایه صحت همه پردازش پاسخ های AT است.

## توابع و منطق کلیدی

### `Token LineReader::feed(uint8_t byte)`
کل منطق در یک تابع؛ قواعد به ترتیب بررسی می شوند:

```cpp
if (byte == '\r') return Token::None;          // CR نادیده گرفته می شود
if (byte == '\n')
{
    if (buf_.empty()) return Token::None;      // خط خالی (CRLF اضافی) → بی اثر
    return Token::Line;                        // پایان یک خط معتبر
}
if (byte == '>' && buf_.empty())
{
    buf_.assign(">");
    return Token::Prompt;                      // پرامپت CMGS فقط وقتی بافر خالی است
}
if (byte == 0) return Token::None;             // NUL نادیده
if (!buf_.push_back(static_cast<char>(byte))) { return Token::Overflow; }
return Token::None;
```

- **`\r` هرگز ذخیره نمی شود** (پروتکل مودم ها CRLF می فرستد؛ فقط LF معنادار است).
- **`>` فقط در ابتدای خط** پرامپت تلقی می شود؛ اگر `>` در میانه یک خط بیاید (مثلاً در متن یک پیامک) مثل کاراکتر عادی در بافر می نشیند.
- **خط خالی** (بلافاصله بعد از LF قبلی) توکنی تولید نمی کند؛ از `Token::Line`های توخالی جلوگیری می شود.
- **سرریز:** اگر `push_back` به دلیل پرشدن بافر (`GSM_LINE_MAX`، پیش فرض ۵۱۲) شکست بخورد، `Token::Overflow` برمی گردد و فراخواننده (`AtEngine::tick`) با `reset()` بافر را دور می ریزد.

متدهای دیگر (در هدر، اینلاین): `line()` اشاره گر به بافر null-terminated، `length()` طول خط، `reset()` تخلیه بافر و `empty()`.

## جریان کاری

```text
بایت های UART ──► feed(byte)
                    │
                    ├─ '\r' / NUL          → None (نادیده)
                    ├─ '\n' + بافر خالی    → None
                    ├─ '\n' + بافر پر      → Line  ──► AtEngine: اکو/نتیجه/URC/observer
                    ├─ '>' + بافر خالی     → Prompt ─► AtEngine: on_prompt (فاز CMGS)
                    ├─ بایت عادی           → push_back → None
                    └─ بافر پر             → Overflow ─► AtEngine: reset + هشدار
```

## نکات فنی

- **بافر `static_string`/`ring` داخلی:** `push_back` بدون تخصیص حافظه پویا؛ اندازه با `GSM_LINE_MAX` (۵۱۲ بایت) در `gsm_config.hpp` قابل تنظیم است.
- **پروتکل `>` بدون CRLF:** برخی مودم ها پرامپت `CMGS` را بدون `\r\n` می فرستند؛ شرط `buf_.empty()` دقیقاً همین حالت را می گیرد و در عین حال `>` داخل payload را خراب نمی کند.
- **تشخیص اکو:** چون متن اکوی فرمان (مثلاً `AT+CMGS="…"`) مثل یک خط عادی تحویل داده می شود، `AtEngine` آن را با `sent_` مقایسه و حذف می کند؛ `LineReader` عمداً در این مورد دخالت نمی کند.
- **NUL حذف می شود** تا رشته های C همیشه null-terminated معتبر بمانند.
- **رفتار در CR تنها:** برخی مودم های قدیمی فقط `\r` می فرستند؛ این پیاده سازی LF-محور است (با `Token::Line` روی `\n`). مستندات `QUIRKS.md` برای سازگاری چنین مودم هایی راهکار دارد.

## ارتباط با سایر فایل ها

- `gsm/core/line_reader.hpp` — تعریف `Token`، متدهای `line()`/`length()`/`reset()`.
- `src/at_engine.cpp` — تنها مصرف کننده: در `tick()` هر بایت UART به `feed` داده می شود.
- `include/gsm/gsm_config.hpp` — `GSM_LINE_MAX` (اندازه بافر خط).
