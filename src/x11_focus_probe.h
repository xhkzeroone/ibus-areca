#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace areca {

// Đọc cửa sổ X11 đang giữ focus (XWayland trên Wayland). Ứng dụng không có
// AT-SPI không báo focus qua đường đó. Chỉ dùng trên một thread.
class X11FocusProbe {
public:
  X11FocusProbe();
  ~X11FocusProbe();

  X11FocusProbe(const X11FocusProbe &) = delete;
  X11FocusProbe &operator=(const X11FocusProbe &) = delete;

  // Tên tiến trình của cửa sổ X11 đang focus, rỗng nếu không có hoặc không đọc
  // được X11.
  std::string focusedProgram();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
