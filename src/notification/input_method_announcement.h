#pragma once

#include <string>

namespace areca {

struct InputMethodIdentity {
  std::string id;
  std::string name;
  std::string icon;
};

// Báo cho nơi khác biết người dùng vừa chuyển sang Areca. Engine gọi announce()
// khi IBus bật engine; lớp con quyết định gửi tới đâu.
class InputMethodAnnouncement {
public:
  explicit InputMethodAnnouncement(InputMethodIdentity identity)
      : identity_(std::move(identity)) {}
  virtual ~InputMethodAnnouncement() = default;

  void announce() { announce(identity_); }

protected:
  virtual void announce(const InputMethodIdentity &identity) = 0;

private:
  InputMethodIdentity identity_;
};

} // namespace areca
