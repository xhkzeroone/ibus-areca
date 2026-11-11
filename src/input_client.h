#pragma once

#include <string>

namespace areca {

// Những gì engine làm được với ô nhập đang focus. IBus tạo một engine cho mỗi
// input context nên không cần truyền context theo từng lời gọi. Engine IBus
// implement bằng libibus, test dùng bản giả.
class InputClient {
public:
  virtual ~InputClient() = default;
  virtual void commitText(const std::string &text) = 0;
  // Gửi một cặp nhấn và nhả Backspace cho ứng dụng.
  virtual void forwardBackspace() = 0;
};

} // namespace areca
