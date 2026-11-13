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
  // Phím client gửi lại về ngay trong cùng chu kỳ phím. Cửa sổ chỉ mở sau lần
  // nhấn đầu, vì phím lặp đầu tiên luôn tới sau độ trễ lặp phím (≥ 100 ms);
  // giữa các phím lặp thì khoảng cách có thể dưới 15 ms nên không mở cửa sổ,
  // nếu không phím lặp thật bị nhận nhầm là phím gửi lại và làm Bamboo lệch
  // với ô nhập. Mỗi lần nhấn chỉ được dùng lại timestamp một lần.
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
               last->second.replayWindowOpen &&
               nowMs - last->second.nowMs <= kReplayWindowMs) {
      time = last->second.time;
      last->second.replayWindowOpen = false;
    } else {
      lastPress_[keyval] = {time, nowMs, !repeat};
    }
    heldKeyval_ = keyval;
    heldTime_ = time;
    return {time, repeat};
  }

private:
  struct Press {
    int time = 0;
    int64_t nowMs = 0;
    bool replayWindowOpen = false;
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
