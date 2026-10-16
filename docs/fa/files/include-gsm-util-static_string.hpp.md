# gsm/include/gsm/util/static_string.hpp

## نقش فایل

رشتهٔ کاراکتری با اندازهٔ ثابت و بدون heap (`StaticString<N>`) — جایگزین `std::string` برای محیط‌های جاسازی‌شده — به‌همراه چند تابع کمکی تجزیهٔ متن AT: بررسی پیشوند، تبدیل به عدد و استخراج فیلد CSV (با در نظر گرفتن گیومه). این فایل ابزار متنی پایهٔ کل کتابخانه است.

## ساختارهای کلیدی

### `template <uint16_t N> class StaticString`
```cpp
template <uint16_t N> class StaticString
{
  public:
    StaticString();
    explicit StaticString(const char* s);

    void     clear();
    bool     empty() const;
    uint16_t size() const;
    static constexpr uint16_t capacity() { return N - 1; }   // N bytes incl. '\0'
    const char* c_str() const;
    char*       data();
    char        operator[](uint16_t i) const;

    bool assign(const char* s, uint16_t n);   // truncates; false if truncated
    bool assign(const char* s);
    bool append(const char* s, uint16_t n);
    bool append(const char* s);
    bool push_back(char c);
    bool printf(const char* fmt, ...);        // __attribute__((format(printf, 2, 3)))
    bool equals(const char* s) const;
    bool starts_with(const char* p) const;
};
```
- ظرفیت متن `N-1` (یک بایت برای null terminator).
- `assign`/`append` همیشه null-terminated نگه می‌دارند؛ در صورت برش خوردن `false` برمی‌گردانند (دادهٔ اضافی بی‌صدا حذف می‌شود).
- `printf` با `vsnprintf` داخلی؛ `false` یعنی خروجی بریده شد.

### توابع کمکی آزاد
```cpp
inline bool str_starts_with(const char* s, const char* p);
```
بررسی پیشوند روی رشتهٔ خام (برای تشخیص `+CMTI:`، `RING` و …).

```cpp
inline bool parse_int(const char* s, int32_t& out);
```
استخراج اولین عدد صحیح (با علامت منفی) از میان متن؛ مبتنی بر حروف ASCII؛ برای پارس فیلدهایی مثل `+CSQ: 18,0` یا ایندکس‌ها.

```cpp
inline uint16_t csv_field(const char* s, uint8_t index, char* dst, uint16_t cap);
```
استخراج فیلد `index` از رشتهٔ جداشده با کاما **با رعایت گیومه** (`"..."`) — مناسب خروجی‌های AT مثل `+CMGL: 1,"REC READ","+989...", ...` یا `+CLCC: 1,0,...` که فیلدهای متنی‌شان کاما دارند.

## جریان کاری

```text
raw AT line from LineReader
    |
    +-> str_starts_with(line, "+CMTI:")   -> route to URC handler
    +-> csv_field(line, 1, buf, cap)      -> extract field 1 ("REC UNREAD", number, ...)
    +-> parse_int(field, &idx)            -> numeric payload
    +-> StaticString<512>::append(...)    -> accumulate body lines
```

نمونه:
```cpp
gsm::StaticString<64> name;
name.assign("SIM800");
name.append("-modem");
if (name.starts_with("SIM")) { /* ... */ }

int32_t idx = -1;
parse_int("+CMTI: \"SM\",3", &idx);          // idx == 3

char f[32];
csv_field("+CLCC: 1,0,0,0,0,\"+98912\"", 5, f, sizeof f);  // f == "+98912"
```

## نکات فنی

- **بدون heap / بدون STL**: آرایهٔ `char[N]` داخل object؛ `capacity()` و `assign` در زمان کامپایل در دسترس‌اند.
- `StaticString` قابل کپی است (memcpy ضمنی)؛ برای Job و ساختارهای داده مناسب است.
- عملیات `assign`/`append` در صورت کمبود جا **برش می‌زنند** و `false` می‌دهند؛ هرگز سرریز نمی‌کنند — اما فراخواننده باید مقدار بازگشتی را برای داده‌های حساس بررسی کند.
- `parse_int` فقط اعداد ASCII را می‌فهمد (برای UCS2-hex از `codec/ucs2.hpp` استفاده کنید) و فقط اولین عدد را می‌گیرد.
- `csv_field` گیومه‌ها را از خروجی حذف می‌کند؛ داخل گیومه، کاما جداکننده نیست.
- `printf` عضو، با `__attribute__((format(printf,2,3)))` تحت بررسی نوع قالب کامپایلر است.
- اندازه‌های معمول در کتابخانه: `GSM_LINE_MAX` (خطوط)، `GSM_CMD_MAX` (دستورها)، `GSM_SMS_TEXT_BYTES` (متن پیامک).

## ارتباط با سایر فایل‌ها

- `core/line_reader.hpp` — بافر خط
- `queue/job.hpp` — فیلدهای `number`/`text`/`ussd_response`
- `sms/sms_types.hpp` — `SmsMessage::number/text/timestamp`
- `status/status_service.hpp` — `ModemStatus::operator_name`
- `codec/phone_number.hpp` — ذخیرهٔ شماره در `StaticString<GSM_SMS_NUMBER_BYTES>`
- `gsm_config.hpp` — اندازه‌های ماکرویی
