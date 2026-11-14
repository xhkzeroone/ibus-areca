#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace areca {

// Tiến trình Wine đặt comm là tên file .exe; comm tối đa 15 ký tự nên đuôi có
// thể bị cắt thành ".ex" hoặc ".e".
bool isWineProgram(std::string_view program);

// Đọc cửa sổ X11 đang giữ focus (XWayland trên Wayland). Ứng dụng Wine không
// có AT-SPI nên không báo focus qua đường đó. Chỉ dùng trên một thread.
class X11FocusProbe {
public:
  X11FocusProbe();
  ~X11FocusProbe();

  X11FocusProbe(const X11FocusProbe &) = delete;
  X11FocusProbe &operator=(const X11FocusProbe &) = delete;

  // Tên tiến trình Wine đang focus, rỗng nếu không có hoặc không đọc được X11.
  std::string focusedWineProgram();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
