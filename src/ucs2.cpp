#include "gsm/codec/ucs2.hpp"
#include <cstring>

namespace gsm::codec
{

namespace
{
inline char hex_digit(uint8_t v)
{ return static_cast<char>(v < 10 ? '0' + v : 'A' + (v - 10)); }
inline int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
uint8_t utf8_next(const char* s, uint32_t& cp)
{
    const uint8_t c0 = static_cast<uint8_t>(s[0]);
    if (c0 < 0x80)
    {
        cp = c0;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80)
    {
        cp = ((c0 & 0x1Fu) << 6) | (static_cast<uint8_t>(s[1]) & 0x3Fu);
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
    {
        cp = ((c0 & 0x0Fu) << 12) | ((static_cast<uint8_t>(s[1]) & 0x3Fu) << 6) |
             (static_cast<uint8_t>(s[2]) & 0x3Fu);
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
        (s[3] & 0xC0) == 0x80)
    {
        cp = ((c0 & 0x07u) << 18) | ((static_cast<uint8_t>(s[1]) & 0x3Fu) << 12) |
             ((static_cast<uint8_t>(s[2]) & 0x3Fu) << 6) | (static_cast<uint8_t>(s[3]) & 0x3Fu);
        return 4;
    }
    return 0;
}
inline bool put_unit(char* out, uint16_t cap, uint16_t& w, uint16_t unit)
{
    if (w + 4 >= cap) return false;
    out[w++] = hex_digit((unit >> 12) & 0xF);
    out[w++] = hex_digit((unit >> 8) & 0xF);
    out[w++] = hex_digit((unit >> 4) & 0xF);
    out[w++] = hex_digit(unit & 0xF);
    return true;
}
} // namespace

uint16_t utf8_to_ucs2_hex(const char* utf8, char* out, uint16_t out_cap)
{
    if (!utf8 || !out || out_cap == 0) return 0;
    uint16_t    w = 0;
    const char* p = utf8;
    while (*p)
    {
        uint32_t      cp   = 0;
        const uint8_t used = utf8_next(p, cp);
        if (!used)
        {
            out[0] = '\0';
            return 0;
        }
        p += used;
        if (cp <= 0xFFFF)
        {
            if (!put_unit(out, out_cap, w, static_cast<uint16_t>(cp)))
            {
                out[w] = '\0';
                return 0;
            }
        }
        else
        {
            cp -= 0x10000u;
            const uint16_t hi = static_cast<uint16_t>(0xD800u + (cp >> 10));
            const uint16_t lo = static_cast<uint16_t>(0xDC00u + (cp & 0x3FFu));
            if (!put_unit(out, out_cap, w, hi) || !put_unit(out, out_cap, w, lo))
            {
                out[w] = '\0';
                return 0;
            }
        }
    }
    out[w] = '\0';
    return w;
}

uint16_t ascii_to_ucs2_hex(const char* ascii, char* out, uint16_t out_cap)
{ return utf8_to_ucs2_hex(ascii, out, out_cap); }

uint16_t ucs2_hex_to_utf8(const char* hex, char* out, uint16_t out_cap)
{
    if (!hex || !out || out_cap == 0) return 0;
    uint16_t w            = 0;
    uint32_t pending_high = 0;
    while (hex[0] && hex[1] && hex[2] && hex[3])
    {
        int a = hex_val(hex[0]), b = hex_val(hex[1]);
        int c = hex_val(hex[2]), d = hex_val(hex[3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) break;
        hex += 4;
        uint32_t cp = static_cast<uint32_t>((a << 12) | (b << 8) | (c << 4) | d);
        if (cp >= 0xD800 && cp <= 0xDBFF)
        {
            pending_high = cp;
            continue;
        }
        if (cp >= 0xDC00 && cp <= 0xDFFF && pending_high)
        {
            cp           = 0x10000u + ((pending_high - 0xD800u) << 10) + (cp - 0xDC00u);
            pending_high = 0;
        }
        if (cp < 0x80)
        {
            if (w + 1 >= out_cap) break;
            out[w++] = static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            if (w + 2 >= out_cap) break;
            out[w++] = static_cast<char>(0xC0 | (cp >> 6));
            out[w++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            if (w + 3 >= out_cap) break;
            out[w++] = static_cast<char>(0xE0 | (cp >> 12));
            out[w++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[w++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            if (w + 4 >= out_cap) break;
            out[w++] = static_cast<char>(0xF0 | (cp >> 18));
            out[w++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out[w++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out[w++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    out[w] = '\0';
    return w;
}

bool looks_like_ucs2_hex(const char* s)
{
    if (!s || !*s) return false;
    uint16_t n = 0;
    for (const char* p = s; *p; ++p, ++n)
        if (hex_val(*p) < 0) return false;
    return n >= 4 && (n % 4) == 0;
}

uint16_t ucs2_units(const char* utf8)
{
    if (!utf8) return 0;
    uint16_t    units = 0;
    const char* p     = utf8;
    while (*p)
    {
        uint32_t      cp   = 0;
        const uint8_t used = utf8_next(p, cp);
        if (!used) break;
        p += used;
        units = static_cast<uint16_t>(units + (cp > 0xFFFF ? 2 : 1));
    }
    return units;
}

bool is_ascii_only(const char* utf8)
{
    if (!utf8) return true;
    for (const char* p = utf8; *p; ++p)
        if (static_cast<uint8_t>(*p) & 0x80) return false;
    return true;
}

} // namespace gsm::codec
