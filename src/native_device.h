#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "im/im.h"

#include "native_key_device.h"

namespace areca {

// Bàn phím ảo của backend Native: RemoteDesktop portal (chỉ bàn phím) + libei
// trên Wayland. Trên X11 hoặc khi phiên libei lỗi thì backend không khả dụng.
class NativeDevice final : public NativeKeyDevice {
public:
  using DebugProvider = std::function<bool()>;

  NativeDevice(im::EventLoop &eventLoop, DebugProvider debugProvider);
  ~NativeDevice() override;

  NativeDevice(const NativeDevice &) = delete;
  NativeDevice &operator=(const NativeDevice &) = delete;

  bool isAvailable() override;
  bool sendBackspace() override;
  bool sendShift(bool press) override;
  bool sendLeft() override;
  bool warmUp();

private:
  bool sendKey(uint32_t code, bool press);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
