#pragma once

#include <cstdint>
#include <unordered_map>

namespace areca {

struct IbusKeyEventStamp {
  int time = 0;
  bool repeat = false;
};

// process-key-event của IBus không có timestamp, còn lõi Areca nhận ra phím bị
// client gửi lại bằng cặp timestamp và keycode giống hệt phím gốc. Lớp này dựng
// lại timestamp ổn định ngay khi nhận phím để lõi giữ nguyên logic.
class IbusKeyEventClock {
public:
  // Phím client gửi lại về ngay trong cùng chu kỳ phím, sớm hơn nhịp lặp phím
  // nhanh nhất (khoảng 16 ms ở 60 lần/giây). Cửa sổ rộng hơn sẽ nhận nhầm phím
  // lặp thật thành phím gửi lại và làm Bamboo lệch với ô nhập.
  static constexpr int kReplayWindowMs = 15;

  // `nowMs` là thời gian đơn điệu tính bằng ms. `forwarded` là cờ
  // IBUS_FORWARD_MASK: phím quay lại engine sau khi đã được chuyển đi.
  IbusKeyEventStamp stamp(uint32_t keyval, bool isRelease, bool forwarded,
                          int64_t nowMs) {
    int time = toEventTime(nowMs);
    if (isRelease) {
      // Release mang timestamp của press tương ứng như event của X11.
      if (heldKeyval_ == keyval) {
        time = heldTime_;
        heldKeyval_ = 0;
        heldTime_ = 0;
      }
      return {time, false};
    }

    // IBus không đánh dấu phím lặp; press đến khi phím đó chưa nhả là lặp.
    const bool repeat = heldKeyval_ == keyval;
    const auto last = lastPress_.find(keyval);
    if (forwarded && last != lastPress_.end()) {
      time = last->second.time;
    } else if (repeat && last != lastPress_.end() &&
               nowMs - last->second.nowMs <= kReplayWindowMs) {
      time = last->second.time;
    } else {
      lastPress_[keyval] = {time, nowMs};
    }
    heldKeyval_ = keyval;
    heldTime_ = time;
    return {time, repeat};
  }

private:
  struct Press {
    int time = 0;
    int64_t nowMs = 0;
  };

  // Lõi coi timestamp 0 là không có thời gian, nên không bao giờ trả về 0.
  static int toEventTime(int64_t nowMs) {
    const auto time = static_cast<int>(nowMs & 0x7fffffff);
    return time == 0 ? 1 : time;
  }

  uint32_t heldKeyval_ = 0;
  int heldTime_ = 0;
  std::unordered_map<uint32_t, Press> lastPress_;
};

} // namespace areca
