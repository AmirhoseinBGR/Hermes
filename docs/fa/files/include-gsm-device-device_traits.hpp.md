# gsm/include/gsm/device/device_traits.hpp

## نقش فایل

توصیف‌های سخت‌افزاری هر خانوادهٔ ماژول GSM در قالب یک struct مقداردهی‌اولیه‌شده در زمان کامپایل (`DeviceTraits`) و سه مقدار آماده برای ماژول‌های پشتیبانی‌شده. تفاوت‌های ظریف بین ماژول‌ها (دستور قطع تماس، محل ذخیرهٔ پیامک، پارامترهای CSMP، timeoutها و اعتماد به `+CLCC`) اینجا متمرکز شده تا کد سرویس‌ها شرطی و پراکنده نشود.

## ساختارهای کلیدی

### `struct DeviceTraits`
```cpp
struct DeviceTraits
{
    const char* name;
    bool        ucs2_recipient_required;  // recipient must also be UCS2-encoded
    const char* csmp_ucs2;                // AT+CSMP=... for UCS2 text
    const char* csmp_gsm;                 // AT+CSMP=... for GSM-7 text
    const char* hangup_cmd;               // ATH or AT+CHUP
    const char* storage_cmd;              // AT+CPMS=...
    bool        supports_clcc;
    bool        supports_cmgd_delete_all;
    uint32_t    boot_timeout_ms;
    uint32_t    prompt_timeout_ms;
    uint32_t    send_timeout_ms;
    // If true, call supervision trusts +CLCC polling as the primary source of
    // truth (module URCs for NO CARRIER / BUSY are unreliable, e.g. M08R).
    bool clcc_authoritative;
};
```

### مقادیر ازپیش‌تعریف‌شده

| Trait | ماژول | hangup | storage | boot | clcc_authoritative |
|---|---|---|---|---|---|
| `kSim800` | SIM800 | `ATH` | `"SM","SM","SM"` | 15000ms | false |
| `kQuectelM08` | M08 | `AT+CHUP` | `"ME","ME","ME"` | 20000ms | false |
| `kQuectelM08R` | M08R | `AT+CHUP` | `"ME","ME","ME"` | 20000ms | **true** |

نمونه:
```cpp
inline constexpr DeviceTraits kQuectelM08R{"M08R",
                                           /*ucs2_recipient_required*/ true,
                                           /*csmp_ucs2*/ "AT+CSMP=17,167,0,8",
                                           /*csmp_gsm*/ "AT+CSMP=17,167,0,0",
                                           /*hangup*/ "AT+CHUP",
                                           /*storage*/ "AT+CPMS=\"ME\",\"ME\",\"ME\"",
                                           /*clcc*/ true,
                                           /*cmgd_all*/ true,
                                           /*boot*/ 20000,
                                           /*prompt*/ GSM_AT_PROMPT_TIMEOUT,
                                           /*send*/ GSM_AT_CMGS_TIMEOUT,
                                           /*clcc_auth*/ true};
```

### ویژگی جدید v0.4: `clcc_authoritative` برای M08R
```cpp
// M08R variant: call-end URCs (NO CARRIER / BUSY / NO ANSWER) are unreliable and
// the module can wedge after interrupted SMS payloads. Call supervision therefore
// relies on +CLCC polling (clcc_authoritative) plus the call watchdog.
```
برای M08R، نظارت تماس به **polling متناوب `+CLCC`** به‌عنوان مرجع اصلی حقیقت تکیه می‌کند؛ URCهای پایان تماس صرفاً کمکی‌اند. این flag مستقیماً رفتار `CallService::monitor` را تغییر می‌دهد.

## جریان کاری

```text
Modem(uart, clock, traits = kSim800, power*)
        |
        |  traits held by reference (must outlive Modem — use constexpr values)
        v
SmsService    : csmp_ucs2 / csmp_gsm / storage_cmd / send_timeout_ms / ucs2_recipient_required
CallService   : hangup_cmd / supports_clcc / clcc_authoritative
StatusService : storage_cmd
Boot sequence : boot_timeout_ms / prompt_timeout_ms
```

استفاده:
```cpp
// Pick the trait matching the hardware; constexpr statics have static storage.
gsm::Modem modem(uart, clock, gsm::kQuectelM08R, &power);
```

## نکات فنی

- `constexpr` و `inline` — بدون هزینهٔ زمان اجرا؛ رشته‌های literal در حافظهٔ فقط‌خواندنی.
- traitها با **اشاره‌گر const** نگهداری می‌شوند؛ از مقدارهای سراسری (`kSim800` و …) یا object با طول عمر کافی استفاده کنید.
- `csmp_ucs2` برای M08/M08R با مقدار DCS=8 و برای SIM800 با 25 متفاوت است — همین تفاوت ظریف دلیل وجود این struct است.
- timeoutهای `prompt` و `send` به‌طور پیش‌فرض از `gsm_config.hpp` می‌آیند اما در trait قابل override ماژول‌محورند.
- افزودن ماژول جدید = تعریف یک `DeviceTraits` جدید؛ نیازی به تغییر سرویس‌ها نیست.

## ارتباط با سایر فایل‌ها

- `gsm_config.hpp` — `GSM_AT_PROMPT_TIMEOUT`، `GSM_AT_CMGS_TIMEOUT`
- `sms/sms_service.hpp` — پارامترهای CSMP، مسیر ذخیره، timeout ارسال، الزام شمارهٔ UCS2
- `voice/call_service.hpp` — `hangup_cmd`، `supports_clcc`، `clcc_authoritative`
- `status/status_service.hpp` — `storage_cmd` برای شمارش پیامک‌ها
- `modem/modem.hpp` — سازندهٔ `Modem` trait را دریافت می‌کند
