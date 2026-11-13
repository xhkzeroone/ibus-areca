#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>

#include "ibus/ibus_key_event_clock.h"
#include "im/im.h"
#include "input_scheduler.h"
#include "rewrite_mode.h"

namespace {

using areca::IbusKeyEventClock;
constexpr uint32_t kBackspace = areca::im::Key_BackSpace;
constexpr uint32_t kA = areca::im::Key_a;

void testReplayKeepsOriginalTime() {
  IbusKeyEventClock clock;
  const auto original = clock.stamp(kBackspace, false, false, 1000);
  assert(!original.repeat);

  // Phím quay lại có cờ forward mang đúng timestamp của phím gốc.
  const auto forwarded = clock.stamp(kBackspace, false, true, 1030);
  assert(forwarded.repeat);
  assert(forwarded.time == original.time);

  // Phím gửi lại không cờ nhưng về ngay trong cùng chu kỳ phím cũng vậy.
  const auto quick = clock.stamp(kBackspace, false, false, 1000 + 10);
  assert(quick.repeat);
  assert(quick.time == original.time);

  // Cửa sổ chỉ dùng được một lần.
  const auto again = clock.stamp(kBackspace, false, false, 1000 + 12);
  assert(again.repeat);
  assert(again.time != original.time);

  const auto release = clock.stamp(kBackspace, true, false, 1100);
  assert(release.time == again.time);

  const auto nextTap = clock.stamp(kBackspace, false, false, 1300);
  assert(!nextTap.repeat);
  assert(nextTap.time != original.time);
}

void testAutoRepeatGetsDistinctTimes() {
  IbusKeyEventClock clock;
  std::set<int> times;
  int64_t now = 5000;
  times.insert(clock.stamp(kBackspace, false, false, now).time);
  // Giữ phím: nhịp lặp đầu sau ~500 ms, sau đó mỗi 20-33 ms.
  now += 500;
  for (int step : {33, 33, 25, 20, 16}) {
    const auto stamp = clock.stamp(kBackspace, false, false, now);
    assert(stamp.repeat);
    times.insert(stamp.time);
    now += step;
  }
  assert(times.size() == 6);

  // Lặp phím nhanh hơn cửa sổ gửi lại: mỗi nhịp lặp vẫn có thời gian riêng.
  IbusKeyEventClock fast;
  std::set<int> fastTimes;
  fastTimes.insert(fast.stamp(kBackspace, false, false, 9000).time);
  for (int64_t at = 9250; at <= 9300; at += 10) {
    fastTimes.insert(fast.stamp(kBackspace, false, false, at).time);
  }
  assert(fastTimes.size() == 7);
}

void testKeysAreTrackedSeparately() {
  IbusKeyEventClock clock;
  const auto backspace = clock.stamp(kBackspace, false, false, 2000);
  clock.stamp(kBackspace, true, false, 2005);
  const auto a = clock.stamp(kA, false, false, 2006);
  assert(!a.repeat);
  assert(a.time != backspace.time);
  // Phím forward của một phím chưa từng nhấn thì lấy thời gian hiện tại.
  IbusKeyEventClock fresh;
  assert(fresh.stamp(kBackspace, false, true, 42).time == 42);
}

void testTimeIsNeverZero() {
  IbusKeyEventClock clock;
  assert(clock.stamp(kA, false, false, 0).time != 0);
  assert(clock.stamp(kBackspace, false, false, int64_t{1} << 31).time != 0);
}

class TestInputContext final : public areca::im::InputContext {
public:
  explicit TestInputContext(areca::im::InputContextManager &manager)
      : areca::im::InputContext(manager, "gnome-text-editor") {}
  ~TestInputContext() override { destroy(); }

protected:
  void commitStringImpl(const std::string &) override {}
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}
};

// Qua đồng hồ, phím gửi lại không làm Bamboo lùi hai lần, còn mỗi nhịp lặp khi
// giữ Backspace vẫn lùi đúng một ký tự như trên fcitx5.
void testRewriteSeesReplayAndHeldBackspace() {
  areca::im::EventLoop loop;
  areca::im::InputContextManager manager;
  areca::RewriteModeHandler::StateFactory factory(
      [](areca::im::InputContext &) {
        return new areca::RewriteInputState("Telex 2", true, true, true,
                                            "Unicode", false, false, 1, {});
      });
  TestInputContext context(manager);
  areca::InputScheduler scheduler(
      loop,
      [&](areca::im::InputContext &ic) {
        return ic.propertyFor(&factory)->engine.get();
      },
      [](areca::im::InputContext &) { return areca::SchedulerTiming{}; },
      [] { return false; },
      [](areca::im::InputContext &, const areca::BambooResult &) {
        return areca::RewriteBackendSelection{};
      });
  areca::RewriteModeHandler handler(
      loop, factory, scheduler, [] { return false; }, [] { return false; },
      [](areca::im::InputContext &, const char *) {}, [] { return false; },
      [](areca::im::InputContext &) { return false; },
      [](areca::im::InputContext &) { return false; },
      [](const areca::im::Key &) { return false; });
  auto *state = context.propertyFor(&factory);
  for (char c : std::string("dduwowcj")) {
    state->engine->process(c, std::string(1, c));
  }
  assert(state->engine->currentText() == "được");

  IbusKeyEventClock clock;
  auto deliver = [&](bool release, bool forwarded, int64_t now) {
    const auto stamp = clock.stamp(kBackspace, release, forwarded, now);
    areca::im::KeyEvent event(&context,
                              areca::im::Key(areca::im::Key_BackSpace,
                                             stamp.repeat
                                                 ? areca::im::KeyState::Repeat
                                                 : areca::im::KeyState::NoState,
                                             22),
                              release, stamp.time);
    handler.handleKeyEvent(event);
    assert(!event.filtered());
  };

  deliver(false, false, 1000);
  assert(state->engine->currentText() == "đượ");
  // GTK gửi lại đúng phím vừa rồi: Bamboo không được lùi thêm.
  deliver(false, true, 1002);
  assert(state->engine->currentText() == "đượ");
  // Giữ phím: mỗi nhịp lặp lùi một ký tự.
  deliver(false, false, 1500);
  assert(state->engine->currentText() == "đư");
  deliver(false, false, 1533);
  assert(state->engine->currentText() == "đ");
  deliver(true, false, 1550);
  assert(!state->backspaceReplay.holding());
}

// Lặp phím nhanh (mỗi 10 ms): mọi nhịp lặp đều là phím thật, không được coi là
// phím gửi lại, nếu không Bamboo sẽ còn nhớ chữ đã xoá trên màn hình.
void testFastHeldBackspaceKeepsBambooInSync() {
  areca::im::EventLoop loop;
  areca::im::InputContextManager manager;
  areca::RewriteModeHandler::StateFactory factory(
      [](areca::im::InputContext &) {
        return new areca::RewriteInputState("Telex 2", true, true, true,
                                            "Unicode", false, false, 1, {});
      });
  TestInputContext context(manager);
  areca::InputScheduler scheduler(
      loop,
      [&](areca::im::InputContext &ic) {
        return ic.propertyFor(&factory)->engine.get();
      },
      [](areca::im::InputContext &) { return areca::SchedulerTiming{}; },
      [] { return false; },
      [](areca::im::InputContext &, const areca::BambooResult &) {
        return areca::RewriteBackendSelection{};
      });
  areca::RewriteModeHandler handler(
      loop, factory, scheduler, [] { return false; }, [] { return false; },
      [](areca::im::InputContext &, const char *) {}, [] { return false; },
      [](areca::im::InputContext &) { return false; },
      [](areca::im::InputContext &) { return false; },
      [](const areca::im::Key &) { return false; });
  auto *state = context.propertyFor(&factory);
  for (char c : std::string("hieepj")) {
    state->engine->process(c, std::string(1, c));
  }
  assert(state->engine->currentText() == "hiệp");

  IbusKeyEventClock clock;
  auto deliver = [&](bool release, int64_t now) {
    const auto stamp = clock.stamp(kBackspace, release, false, now);
    areca::im::KeyEvent event(&context,
                              areca::im::Key(areca::im::Key_BackSpace,
                                             stamp.repeat
                                                 ? areca::im::KeyState::Repeat
                                                 : areca::im::KeyState::NoState,
                                             22),
                              release, stamp.time);
    handler.handleKeyEvent(event);
    assert(!event.filtered());
  };

  // Bamboo có thể dời dấu sau khi lùi, nên chỉ đếm số ký tự còn lại.
  auto remaining = [&] {
    return areca::im::utf8::length(state->engine->currentText());
  };
  deliver(false, 1000);
  assert(remaining() == 3);
  deliver(false, 1250);
  assert(remaining() == 2);
  deliver(false, 1260);
  assert(remaining() == 1);
  deliver(false, 1270);
  assert(remaining() == 0);
  deliver(true, 1280);
}

} // namespace

int main() {
  testReplayKeepsOriginalTime();
  testAutoRepeatGetsDistinctTimes();
  testKeysAreTrackedSeparately();
  testTimeIsNeverZero();
  testRewriteSeesReplayAndHeldBackspace();
  testFastHeldBackspaceKeepsBambooInSync();
  std::cout << "ibus_key_event_clock_test passed\n";
  return 0;
}
