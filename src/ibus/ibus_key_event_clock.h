#pragma once

#include <cstdint>

namespace areca {

struct IbusKeyEventStamp {
  int time = 0;
  bool repeat = false;
};

// IBus process-key-event has no timestamp argument. Reconstruct a stable event
// time at the adapter boundary so the shared key handlers can use Fcitx's
// timestamp/keycode replay check unchanged.
class IbusKeyEventClock {
public:
  IbusKeyEventStamp stamp(uint32_t keyval, bool isBackspace, bool isRelease,
                          bool forwarded, int nowMs) {
    int time = nowMs;
    bool repeat = false;
    if (isRelease) {
      if (heldKeyval_ == keyval) {
        time = heldKeyTime_;
        heldKeyval_ = 0;
        heldKeyTime_ = 0;
      }
    } else {
      repeat = heldKeyval_ == keyval;
      if (forwarded && isBackspace && lastBackspaceTime_ != 0) {
        time = lastBackspaceTime_;
      } else if (repeat && nowMs >= heldKeyTime_ &&
                 nowMs - heldKeyTime_ <= kReplayWindowMs) {
        // GTK replay returns during the same key cycle; hardware repeat starts
        // later and receives its own timestamp.
        time = heldKeyTime_;
      }
      heldKeyval_ = keyval;
      heldKeyTime_ = time;
      if (isBackspace && !forwarded) {
        lastBackspaceTime_ = time;
      }
    }
    return {time, repeat};
  }

private:
  static constexpr int kReplayWindowMs = 50;
  uint32_t heldKeyval_ = 0;
  int heldKeyTime_ = 0;
  int lastBackspaceTime_ = 0;
};

} // namespace areca
