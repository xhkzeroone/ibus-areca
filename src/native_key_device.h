#pragma once

namespace areca {

// Thiết bị phát phím mà các backend Native dùng để sửa chữ.
class NativeKeyDevice {
public:
  virtual ~NativeKeyDevice() = default;
  virtual bool isAvailable() = 0;
  virtual bool sendBackspace() = 0;
  virtual bool sendShift(bool press) {
    (void)press;
    return false;
  }
  virtual bool sendLeft() { return false; }
};

} // namespace areca
