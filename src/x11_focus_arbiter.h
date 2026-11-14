#pragma once

#include <chrono>
#include <string>
#include <utility>

namespace areca {

// Chọn giữa tên từ AT-SPI và tên từ cửa sổ X11 đang focus. Ứng dụng không có
// AT-SPI (Wine, game, Qt hoặc Java không bật a11y) không bao giờ báo focus nên
// AT-SPI giữ tên của app trước đó. Sau khi X11 đổi focus, nếu AT-SPI vẫn im
// lặng quá thời gian chờ thì tên X11 được dùng.
class X11FocusArbiter {
public:
  using Clock = std::chrono::steady_clock;

  explicit X11FocusArbiter(
      std::chrono::milliseconds grace = std::chrono::milliseconds(300))
      : grace_(grace) {}

  // Gọi mỗi lần hỏi X11; program rỗng nghĩa là không có cửa sổ X11 focus.
  void noteX11Program(const std::string &program, Clock::time_point now) {
    if (!seen_ || program != x11Program_) {
      seen_ = true;
      x11Program_ = program;
      changedAt_ = now;
      published_.clear();
    }
  }

  // Có sự kiện focus từ AT-SPI nên tên AT-SPI đáng tin cho cửa sổ này.
  void noteAtspiFocus(Clock::time_point now) {
    atspiAt_ = now;
    published_.clear();
  }

  // Tên X11 cần dùng thay cho AT-SPI, rỗng nếu cứ dùng AT-SPI.
  const std::string &program(Clock::time_point now) {
    if (published_.empty() && !x11Program_.empty() && atspiAt_ < changedAt_ &&
        now - changedAt_ >= grace_) {
      published_ = x11Program_;
    }
    return published_;
  }

private:
  std::chrono::milliseconds grace_;
  bool seen_ = false;
  std::string x11Program_;
  std::string published_;
  Clock::time_point changedAt_{};
  Clock::time_point atspiAt_{};
};

} // namespace areca
