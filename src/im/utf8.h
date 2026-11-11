#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Các hàm UTF-8 cùng ngữ nghĩa với bản Areca chạy trên fcitx5, để code chép
// sang giữ nguyên lời gọi.
namespace areca::im::utf8 {

namespace detail {

inline constexpr std::size_t kInvalidLength = static_cast<std::size_t>(-1);

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

// Số ký tự của chuỗi, `kInvalidLength` nếu chuỗi sai chuẩn.
inline std::size_t length(std::string_view text) {
  std::size_t count = 0;
  for (std::size_t offset = 0; offset < text.size(); ++count) {
    uint32_t codepoint = 0;
    const auto bytes = decode(text, offset, codepoint);
    if (!bytes) {
      return kInvalidLength;
    }
    offset += bytes;
  }
  return count;
}

inline bool validate(std::string_view text) {
  return length(text) != kInvalidLength;
}

// Số byte của `chars` ký tự đầu, `kInvalidLength` nếu chuỗi ngắn hơn hoặc sai
// chuẩn.
inline std::size_t byteLength(std::string_view text, std::size_t chars) {
  std::size_t offset = 0;
  for (std::size_t i = 0; i < chars; ++i) {
    uint32_t codepoint = 0;
    const auto bytes = decode(text, offset, codepoint);
    if (!bytes) {
      return kInvalidLength;
    }
    offset += bytes;
  }
  return offset;
}

} // namespace detail

inline constexpr std::size_t INVALID_LENGTH = static_cast<std::size_t>(-1);

inline bool validate(std::string_view text) { return detail::validate(text); }

// Số ký tự; chuỗi sai chuẩn trả về số ký tự hợp lệ tới chỗ lỗi để không làm vỡ
// các phép so sánh dùng giá trị này.
inline std::size_t length(std::string_view text) {
  std::size_t count = 0;
  for (std::size_t offset = 0; offset < text.size(); ++count) {
    uint32_t codepoint = 0;
    const auto bytes = detail::decode(text, offset, codepoint);
    if (!bytes) {
      break;
    }
    offset += bytes;
  }
  return count;
}

inline std::size_t lengthValidated(std::string_view text) {
  return detail::length(text);
}

inline bool isValidChar(uint32_t codepoint) {
  return codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF);
}

inline constexpr uint32_t kInvalidChar = static_cast<uint32_t>(-1);

// Byte của `chars` ký tự đầu, âm nếu chuỗi ngắn hơn hoặc sai chuẩn.
template <typename Iter> int ncharByteLength(Iter begin, std::size_t chars) {
  int bytes = 0;
  for (std::size_t i = 0; i < chars; ++i) {
    const auto first = static_cast<unsigned char>(*begin);
    int length = 1;
    if (first >= 0xF0) {
      length = 4;
    } else if (first >= 0xE0) {
      length = 3;
    } else if (first >= 0xC0) {
      length = 2;
    }
    bytes += length;
    begin += length;
  }
  return bytes;
}

template <typename Iter> Iter nextNChar(Iter begin, std::size_t chars) {
  return begin + ncharByteLength(begin, chars);
}

// Đọc một ký tự, trả iterator sau ký tự. Ký tự lỗi cho `kInvalidChar` và chỉ
// tiến một byte.
template <typename Iter>
Iter getNextChar(Iter begin, Iter end, uint32_t *codepoint) {
  const std::string_view rest(&*begin, static_cast<std::size_t>(end - begin));
  uint32_t value = 0;
  const auto bytes = detail::decode(rest, 0, value);
  if (!bytes) {
    *codepoint = kInvalidChar;
    return begin + 1;
  }
  *codepoint = value;
  return begin + static_cast<std::ptrdiff_t>(bytes);
}

class UTF8CharIterator {
public:
  using Iter = std::string::const_iterator;
  UTF8CharIterator(Iter current, Iter end) : current_(current), end_(end) {
    update();
  }
  uint32_t operator*() const { return codepoint_; }
  UTF8CharIterator &operator++() {
    current_ = next_;
    update();
    return *this;
  }
  bool operator==(const UTF8CharIterator &other) const {
    return current_ == other.current_;
  }
  bool operator!=(const UTF8CharIterator &other) const {
    return !(*this == other);
  }
  Iter base() const { return current_; }
  std::ptrdiff_t charLength() const { return next_ - current_; }

private:
  void update() {
    if (current_ == end_) {
      next_ = end_;
      codepoint_ = 0;
      return;
    }
    next_ = getNextChar(current_, end_, &codepoint_);
  }

  Iter current_;
  Iter end_;
  Iter next_;
  uint32_t codepoint_ = 0;
};

inline std::string UCS4ToUTF8(uint32_t codepoint) {
  std::string out;
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0x10FFFF) {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return out;
}

} // namespace areca::im::utf8
