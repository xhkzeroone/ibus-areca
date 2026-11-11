#pragma once

#include <cstdint>
#include <functional>

#include "bamboo_engine_adapter.h"
#include "input_scheduler.h"

namespace areca {

struct KeyInput {
  uint32_t keyval = 0;
  uint32_t state = 0;
  // Ký tự Unicode của phím, 0 nếu phím không sinh ký tự.
  uint32_t codepoint = 0;
};

enum class KeyDisposition {
  // Không nhận, IBus giao phím gốc cho ứng dụng.
  Forward,
  // Đã nhận, scheduler sẽ commit kết quả.
  Accept,
};

// Quyết định từng phím ở chế độ Rewrite: phím nào vào hàng đợi, phím nào để
// ứng dụng tự xử lý, khi nào reset Bamboo.
class RewriteMode final {
public:
  using DebugProvider = std::function<bool()>;

  RewriteMode(VietnameseEngine &engine, InputScheduler &scheduler,
              DebugProvider debugProvider);
  RewriteMode(const RewriteMode &) = delete;
  RewriteMode &operator=(const RewriteMode &) = delete;

  KeyDisposition handleKey(const KeyInput &key);
  // Reset do ứng dụng hoặc mất focus. Bị bỏ qua trong cửa sổ chặn reset.
  void requestReset();
  void setPasswordField(bool password) { passwordField_ = password; }

private:
  VietnameseEngine &engine_;
  InputScheduler &scheduler_;
  DebugProvider debugProvider_;
  bool passwordField_ = false;
};

} // namespace areca
