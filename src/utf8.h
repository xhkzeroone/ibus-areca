#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace areca::utf8 {

inline constexpr std::size_t kInvalid = static_cast<std::size_t>(-1);

// Giải mã một ký tự tại `offset`, trả về số byte của ký tự hoặc 0 nếu sai
// chuẩn. Từ chối dạng mã hoá dài thừa và surrogate để chuỗi đã qua kiểm tra có
// thể so sánh theo byte.
inline std::size_t decode(std::string_view text, std::size_t offset,
                          uint32_t &codepoint) {
  if (offset >= text.size()) {
    return 0;
  }
  const auto first = static_cast<unsigned char>(text[offset]);
  std::size_t length = 0;
  if (first < 0x80) {
    codepoint = first;
    return 1;
  }
  if ((first & 0xE0) == 0xC0) {
    codepoint = first & 0x1F;
    length = 2;
  } else if ((first & 0xF0) == 0xE0) {
    codepoint = first & 0x0F;
    length = 3;
  } else if ((first & 0xF8) == 0xF0) {
    codepoint = first & 0x07;
    length = 4;
  } else {
    return 0;
  }
  if (offset + length > text.size()) {
    return 0;
  }
  for (std::size_t i = 1; i < length; ++i) {
    const auto next = static_cast<unsigned char>(text[offset + i]);
    if ((next & 0xC0) != 0x80) {
      return 0;
    }
    codepoint = (codepoint << 6) | (next & 0x3F);
  }
  const bool overlong = (length == 2 && codepoint < 0x80) ||
                        (length == 3 && codepoint < 0x800) ||
                        (length == 4 && codepoint < 0x10000);
  if (overlong || codepoint > 0x10FFFF ||
      (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
    return 0;
  }
  return length;
}

// Số ký tự của chuỗi, `kInvalid` nếu chuỗi sai chuẩn.
inline std::size_t length(std::string_view text) {
  std::size_t count = 0;
  for (std::size_t offset = 0; offset < text.size(); ++count) {
    uint32_t codepoint = 0;
    const auto bytes = decode(text, offset, codepoint);
    if (!bytes) {
      return kInvalid;
    }
    offset += bytes;
  }
  return count;
}

inline bool validate(std::string_view text) { return length(text) != kInvalid; }

// Số byte của `chars` ký tự đầu, `kInvalid` nếu chuỗi ngắn hơn hoặc sai chuẩn.
inline std::size_t byteLength(std::string_view text, std::size_t chars) {
  std::size_t offset = 0;
  for (std::size_t i = 0; i < chars; ++i) {
    uint32_t codepoint = 0;
    const auto bytes = decode(text, offset, codepoint);
    if (!bytes) {
      return kInvalid;
    }
    offset += bytes;
  }
  return offset;
}

} // namespace areca::utf8
