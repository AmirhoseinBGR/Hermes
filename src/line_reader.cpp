#include "gsm/core/line_reader.hpp"

namespace gsm
{

Token LineReader::feed(uint8_t byte)
{
    if (byte == '\r') return Token::None;
    if (byte == '\n')
    {
        if (buf_.empty()) return Token::None;
        return Token::Line;
    }
    if (byte == '>' && buf_.empty())
    {
        buf_.assign(">");
        return Token::Prompt;
    }
    if (byte == 0) return Token::None;
    if (!buf_.push_back(static_cast<char>(byte))) { return Token::Overflow; }
    return Token::None;
}

} // namespace gsm
