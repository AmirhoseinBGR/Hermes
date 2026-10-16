#pragma once
#include <cstdint>

namespace gsm::codec
{

uint16_t utf8_to_ucs2_hex(const char* utf8, char* out, uint16_t out_cap);
uint16_t ucs2_hex_to_utf8(const char* hex, char* out, uint16_t out_cap);
uint16_t ascii_to_ucs2_hex(const char* ascii, char* out, uint16_t out_cap);

bool     looks_like_ucs2_hex(const char* s);
uint16_t ucs2_units(const char* utf8);
bool     is_ascii_only(const char* utf8);

} // namespace gsm::codec
