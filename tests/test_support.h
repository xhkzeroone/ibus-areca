#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glib.h>

#include "input_client.h"
#include "utf8.h"

namespace areca::test {

// Ô nhập giả: giữ text như ứng dụng thật sẽ có và ghi lại thứ tự thao tác.
class FakeClient final : public InputClient {
public:
  void commitText(const std::string &value) override {
    operations.push_back("commit:" + value);
    text += value;
  }
  void forwardBackspace() override {
    operations.push_back("backspace");
    const auto length = utf8::length(text);
    if (length != utf8::kInvalid && length > 0) {
      text.erase(utf8::byteLength(text, length - 1));
    }
  }

  std::string text;
  std::vector<std::string> operations;
};

// Chạy main loop GLib tới khi điều kiện đúng hoặc hết thời gian.
inline bool runUntil(const std::function<bool()> &done,
                     uint32_t timeoutMs = 2000) {
  const int64_t deadline =
      g_get_monotonic_time() + static_cast<int64_t>(timeoutMs) * 1000;
  while (!done()) {
    if (g_get_monotonic_time() > deadline) {
      return false;
    }
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(200);
  }
  return true;
}

inline void runFor(uint32_t durationMs) {
  const int64_t deadline =
      g_get_monotonic_time() + static_cast<int64_t>(durationMs) * 1000;
  runUntil([&]() { return g_get_monotonic_time() > deadline; },
           durationMs + 1000);
}

} // namespace areca::test
