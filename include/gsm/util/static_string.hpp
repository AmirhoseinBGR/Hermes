#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace gsm
{

template <uint16_t N> class StaticString
{
  public:
    StaticString() { buf_[0] = '\0'; }
    explicit StaticString(const char* s) { assign(s); }

    void clear()
    {
        len_    = 0;
        buf_[0] = '\0';
    }
    bool                      empty() const { return len_ == 0; }
    uint16_t                  size() const { return len_; }
    static constexpr uint16_t capacity() { return N - 1; }
    const char*               c_str() const { return buf_; }
    char*                     data() { return buf_; }
    char                      operator[](uint16_t i) const { return buf_[i]; }

    bool assign(const char* s, uint16_t n)
    {
        const uint16_t copy = n > capacity() ? capacity() : n;
        if (s && copy) std::memcpy(buf_, s, copy);
        len_       = copy;
        buf_[len_] = '\0';
        return copy == n;
    }
    bool assign(const char* s) { return assign(s, s ? static_cast<uint16_t>(std::strlen(s)) : 0); }

    bool append(const char* s, uint16_t n)
    {
        const uint16_t room = static_cast<uint16_t>(capacity() - len_);
        const uint16_t copy = n > room ? room : n;
        if (s && copy) std::memcpy(buf_ + len_, s, copy);
        len_       = static_cast<uint16_t>(len_ + copy);
        buf_[len_] = '\0';
        return copy == n;
    }
    bool append(const char* s) { return append(s, s ? static_cast<uint16_t>(std::strlen(s)) : 0); }
    bool push_back(char c) { return append(&c, 1); }

    bool printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list ap;
        va_start(ap, fmt);
        const int n = vsnprintf(buf_, N, fmt, ap);
        va_end(ap);
        if (n < 0)
        {
            clear();
            return false;
        }
        len_       = (n >= N) ? capacity() : static_cast<uint16_t>(n);
        buf_[len_] = '\0';
        return n < N;
    }

    bool equals(const char* s) const { return s && std::strcmp(buf_, s) == 0; }
    bool starts_with(const char* p) const
    {
        if (!p) return false;
        const uint16_t n = static_cast<uint16_t>(std::strlen(p));
        return n <= len_ && std::memcmp(buf_, p, n) == 0;
    }

  private:
    char     buf_[N];
    uint16_t len_ = 0;
};

inline bool str_starts_with(const char* s, const char* p)
{
    if (!s || !p) return false;
    while (*p)
    {
        if (*s++ != *p++) return false;
    }
    return true;
}

inline bool parse_int(const char* s, int32_t& out)
{
    if (!s) return false;
    while (*s && (*s < '0' || *s > '9') && *s != '-')
        ++s;
    if (!*s) return false;
    bool neg = false;
    if (*s == '-')
    {
        neg = true;
        ++s;
    }
    if (*s < '0' || *s > '9') return false;
    int32_t v = 0;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s - '0');
        ++s;
    }
    out = neg ? -v : v;
    return true;
}

inline uint16_t csv_field(const char* s, uint8_t index, char* dst, uint16_t cap)
{
    if (!s || !dst || !cap) return 0;
    uint8_t  field  = 0;
    bool     quoted = false;
    uint16_t w      = 0;
    for (; *s; ++s)
    {
        if (*s == '"')
        {
            quoted = !quoted;
            continue;
        }
        if (*s == ',' && !quoted)
        {
            if (field == index) break;
            ++field;
            w = 0;
            continue;
        }
        if (field == index && w + 1u < cap) dst[w++] = *s;
    }
    dst[w] = '\0';
    return w;
}

} // namespace gsm
