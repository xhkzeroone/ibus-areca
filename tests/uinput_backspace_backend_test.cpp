#include <cassert>
#include <functional>
#include <iostream>
#include <linux/input.h>
#include <string>
#include <vector>

#include "im/im.h"

#include "uinput_backspace_backend.h"
#include "uinput_device.h"

namespace {
struct SentKey {
  uint16_t code;
  int value;
  uint64_t time;
};
class FakeDevice final : public areca::UinputDevice {
public:
  FakeDevice() : UinputDevice([] { return false; }) {}
  bool ensureDevice() override { return true; }
  void sendKeyEvent(uint16_t code, int value) override {
    keys.push_back({code, value, areca::im::now(CLOCK_MONOTONIC)});
    if (onKey)
      onKey(code, value);
  }
  std::vector<SentKey> keys;
  std::function<void(uint16_t, int)> onKey;
};
class Context final : public areca::im::InputContext {
public:
  explicit Context(areca::im::InputContextManager &manager)
      : InputContext(manager, "backspace-test") {}
  ~Context() override { destroy(); }
  void commitStringImpl(const std::string &text) override {
    commits += text;
    commitTime = areca::im::now(CLOCK_MONOTONIC);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}
  std::string commits;
  uint64_t commitTime = 0;
};

void run(bool earlyAck, uint32_t waitMs, uint32_t count) {
  areca::im::EventLoop loop;
  areca::im::InputContextManager manager;
  Context context(manager);
  FakeDevice device;
  areca::AdaptiveWait adaptive;
  areca::UinputBackspaceBackend backend(loop, device, adaptive,
                                        [] { return false; });
  areca::RewritePlan plan;
  plan.transactionId = 1;
  plan.backspaceCount = count;
  plan.backspaceDelayMs = 20;
  plan.uinputBackspaceAckTimeoutMs = 40;
  plan.afterBackspaceWaitMs = waitMs;
  plan.commitText = "X";
  int completed = 0;
  uint64_t ackTime = 0;
  auto deliver = [&](bool release, bool repeat) {
    areca::im::KeyEvent event(&context,
                              areca::im::Key(areca::im::Key_BackSpace,
                                             repeat
                                                 ? areca::im::KeyState::Repeat
                                                 : areca::im::KeyState::NoState,
                                             22),
                              release, 1234);
    assert(backend.handleBackspace(event));
    return event.filtered() && event.accepted();
  };
  assert(backend.apply(
             context, plan, [&](uint64_t id, areca::RewriteOutcome result) {
               assert(id == 1 && result == areca::RewriteOutcome::Succeeded);
               ++completed;
               loop.exit();
             }) == areca::ApplyStatus::Pending);
  std::unique_ptr<areca::im::EventSourceTime> ackTimer;
  if (earlyAck) {
    // Đã phát một Backspace, nhưng hai press cùng time/code làm đủ ngưỡng.
    // Timer gửi phím thứ hai phải tiếp tục dù timer commit đã bắt đầu.
    ackTimer = loop.addTimeEvent(CLOCK_MONOTONIC,
                                 areca::im::now(CLOCK_MONOTONIC) + 1000, 1,
                                 [&](areca::im::EventSourceTime *, uint64_t) {
                                   assert(device.keys.size() == 2);
                                   ackTime = areca::im::now(CLOCK_MONOTONIC);
                                   assert(!deliver(false, false));
                                   assert(!deliver(false, true));
                                   assert(!deliver(true, false));
                                   assert(!deliver(true, false));
                                   // GTK replay vượt quá số Backspace phát ra
                                   // phải bị chặn, không tạo lần xóa thứ ba.
                                   assert(deliver(false, true));
                                   return false;
                                 });
    ackTimer->setOneShot();
  }
  auto watchdog = loop.addTimeEvent(
      CLOCK_MONOTONIC, areca::im::now(CLOCK_MONOTONIC) + 2000000, 1,
      [](areca::im::EventSourceTime *, uint64_t) {
        assert(false);
        return false;
      });
  loop.exec();
  assert(completed == 1 && !backend.hasPending());
  assert(context.commits == "X");
  assert(device.keys.size() == count * 2);
  for (size_t i = 0; i < device.keys.size(); ++i) {
    assert(device.keys[i].code == KEY_BACKSPACE);
    assert(device.keys[i].value == (i % 2 == 0 ? 1 : 0));
  }
  if (count) {
    assert(context.commitTime >= device.keys.back().time);
    if (earlyAck) {
      assert(context.commitTime >= ackTime + waitMs * 1000);
    } else {
      assert(context.commitTime >= device.keys.back().time + 40000);
    }
  }
}

void duplicateReplayDoesNotReachClient() {
  areca::im::EventLoop loop;
  areca::im::InputContextManager manager;
  Context context(manager);
  FakeDevice device;
  areca::AdaptiveWait adaptive;
  areca::UinputBackspaceBackend backend(loop, device, adaptive,
                                        [] { return false; });
  areca::RewritePlan plan;
  plan.transactionId = 2;
  plan.backspaceCount = 1;
  plan.uinputBackspaceAckTimeoutMs = 40;
  plan.afterBackspaceWaitMs = 1;
  int completed = 0;
  assert(backend.apply(
             context, plan, [&](uint64_t id, areca::RewriteOutcome result) {
               assert(id == 2 && result == areca::RewriteOutcome::Succeeded);
               ++completed;
               loop.exit();
             }) == areca::ApplyStatus::Pending);

  bool originalForwarded = false;
  bool replayConsumed = false;
  auto ackTimer = loop.addTimeEvent(
      CLOCK_MONOTONIC, areca::im::now(CLOCK_MONOTONIC) + 1000, 1,
      [&](areca::im::EventSourceTime *, uint64_t) {
        auto press = [&](bool repeat) {
          areca::im::KeyEvent event(
              &context,
              areca::im::Key(areca::im::Key_BackSpace,
                             repeat ? areca::im::KeyState::Repeat
                                    : areca::im::KeyState::NoState,
                             22),
              false, 2000);
          assert(backend.handleBackspace(event));
          return event.filtered() && event.accepted();
        };
        originalForwarded = !press(false);
        replayConsumed = press(true);
        for (const bool release : {true, true}) {
          areca::im::KeyEvent event(
              &context, areca::im::Key(areca::im::Key_BackSpace, {}, 22),
              release, 2000);
          assert(backend.handleBackspace(event));
        }
        return false;
      });
  ackTimer->setOneShot();
  auto watchdog = loop.addTimeEvent(
      CLOCK_MONOTONIC, areca::im::now(CLOCK_MONOTONIC) + 2000000, 1,
      [](areca::im::EventSourceTime *, uint64_t) {
        assert(false);
        return false;
      });
  loop.exec();
  assert(completed == 1 && originalForwarded && replayConsumed);
  assert(device.keys.size() == 2);
}
} // namespace
int main() {
  run(true, 60, 2); // Delay đủ rộng: tiếp tục gửi trong lúc đang chờ commit.
  run(true, 1, 2);  // Delay quá ngắn: không commit trước khi phát hết phím.
  run(false, 1, 2); // Thiếu ACK: timeout vẫn hoàn tất một lần.
  run(false, 1, 0); // Không cần xóa: commit trực tiếp sau delay.
  duplicateReplayDoesNotReachClient();
  std::cout << "Uinput Backspace timer tests passed\n";
}
