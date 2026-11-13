#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "im/im.h"

#include "native_key_device.h"

namespace areca {

// Bàn phím ảo của backend Native: RemoteDesktop portal (chỉ bàn phím) + libei
// trên Wayland. Trên X11 hoặc khi phiên libei lỗi thì backend không khả dụng.
class NativeDevice final : public NativeKeyDevice {
public:
  using DebugProvider = std::function<bool()>;
  // Thuộc tính của org.freedesktop.portal.RemoteDesktop.
  struct PortalInfo {
    uint32_t version = 0;
    uint32_t availableDeviceTypes = 0;
  };
  // Đọc thuộc tính portal bất đồng bộ rồi gọi lại trên event loop chính;
  // nullopt khi không có portal RemoteDesktop. Bỏ trống thì dùng D-Bus session.
  using PortalProbe =
      std::function<void(std::function<void(std::optional<PortalInfo>)>)>;

  NativeDevice(im::EventLoop &eventLoop, DebugProvider debugProvider,
               PortalProbe portalProbe = {});
  ~NativeDevice() override;

  NativeDevice(const NativeDevice &) = delete;
  NativeDevice &operator=(const NativeDevice &) = delete;

  bool isAvailable() override;
  bool sendBackspace() override;
  bool sendShift(bool press) override;
  bool sendLeft() override;
  bool warmUp();
  // Hỏi portal có RemoteDesktop kèm ConnectToEIS và bàn phím không, chỉ đọc
  // thuộc tính nên không mở hộp thoại xin quyền. Kết quả về bất đồng bộ.
  void probeSupport();
  // Có thể dùng được hay không mà không xin quyền: false khi phiên không phải
  // Wayland, chưa dò xong, portal không hỗ trợ hoặc libei đã lỗi.
  bool isSupported() const;

private:
  bool sendKey(uint32_t code, bool press);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace areca
