#pragma once
#include "gsm/gsm_config.hpp"
#include "gsm/util/static_string.hpp"
#include <cstdint>

namespace gsm
{

enum class Token : uint8_t
{
    None,
    Line,
    Prompt,
    Overflow
};

class LineReader
{
  public:
    Token       feed(uint8_t byte);
    const char* line() const { return buf_.c_str(); }
    uint16_t    length() const { return buf_.size(); }
    void        reset() { buf_.clear(); }

  private:
    StaticString<GSM_LINE_MAX> buf_;
};

} // namespace gsm
