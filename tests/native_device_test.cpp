#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>

#include "im/im.h"
#include "native_device.h"

namespace {

using areca::NativeDevice;
using Reply = std::function<void(std::optional<NativeDevice::PortalInfo>)>;

// Portal giả: giữ lại lời gọi để test tự trả lời.
struct FakePortal {
  int calls = 0;
  Reply reply;

  NativeDevice::PortalProbe probe() {
    return [this](Reply done) {
      ++calls;
      reply = std::move(done);
    };
  }
};

void useWayland(bool wayland) {
  if (wayland) {
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
  } else {
    setenv("XDG_SESSION_TYPE", "x11", 1);
    unsetenv("WAYLAND_DISPLAY");
  }
}

std::unique_ptr<NativeDevice> makeDevice(areca::im::EventLoop &loop,
                                         FakePortal &portal) {
  return std::make_unique<NativeDevice>(
      loop, [] { return false; }, portal.probe());
}

void testSupportedPortal() {
  useWayland(true);
  areca::im::EventLoop loop;
  FakePortal portal;
  auto device = makeDevice(loop, portal);
  // Chưa dò thì chưa hiện Native.
  assert(!device->isSupported());
  device->probeSupport();
  device->probeSupport(); // Chỉ hỏi portal một lần.
  assert(portal.calls == 1);
  assert(!device->isSupported());
  portal.reply(NativeDevice::PortalInfo{2, 1 | 2});
  assert(device->isSupported());
}

void testUnsupportedPortals() {
  useWayland(true);
  const std::optional<NativeDevice::PortalInfo> replies[] = {
      std::nullopt,                   // Không có RemoteDesktop.
      NativeDevice::PortalInfo{1, 1}, // Chưa có ConnectToEIS.
      NativeDevice::PortalInfo{2, 2}, // Không cấp bàn phím.
  };
  for (const auto &reply : replies) {
    areca::im::EventLoop loop;
    FakePortal portal;
    auto device = makeDevice(loop, portal);
    device->probeSupport();
    portal.reply(reply);
    assert(!device->isSupported());
    // Máy không hỗ trợ thì không mở phiên xin quyền.
    assert(!device->isAvailable());
  }
}

// Cần gửi phím lúc đang dò thì chưa mở phiên; portal báo không hỗ trợ thì
// thôi hẳn.
void testSessionWaitsForProbe() {
  useWayland(true);
  areca::im::EventLoop loop;
  FakePortal portal;
  auto device = makeDevice(loop, portal);
  device->probeSupport();
  assert(!device->isAvailable());
  portal.reply(std::nullopt);
  assert(!device->isSupported());
  assert(!device->isAvailable());
}

void testX11NeverProbes() {
  useWayland(false);
  areca::im::EventLoop loop;
  FakePortal portal;
  auto device = makeDevice(loop, portal);
  device->probeSupport();
  assert(portal.calls == 0);
  assert(!device->isSupported());
  assert(!device->isAvailable());
}

void testLateReplyAfterDestroy() {
  useWayland(true);
  areca::im::EventLoop loop;
  FakePortal portal;
  auto device = makeDevice(loop, portal);
  device->probeSupport();
  device.reset();
  // Câu trả lời về muộn không được chạm vào device đã huỷ.
  portal.reply(NativeDevice::PortalInfo{2, 1});
}

} // namespace

int main() {
  testSupportedPortal();
  testUnsupportedPortals();
  testSessionWaitsForProbe();
  testX11NeverProbes();
  testLateReplyAfterDestroy();
  std::cout << "native_device_test passed\n";
  return 0;
}
