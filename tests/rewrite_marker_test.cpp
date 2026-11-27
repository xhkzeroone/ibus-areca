#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "im/im.h"

#include "input_scheduler.h"

namespace {

const std::string kMarker = "\xC2\xB7";

class TestInputContext final : public areca::im::InputContext {
public:
  explicit TestInputContext(areca::im::InputContextManager &manager)
      : areca::im::InputContext(manager, "test-app") {}
  ~TestInputContext() override { destroy(); }

  void commitStringImpl(const std::string &text) override {
    events.push_back("commit:" + text);
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}

  std::vector<std::string> events;
};

// "a" ghép với "a" trước thành "â": xoá 1 ký tự rồi chèn "â".
class TestEngine final : public areca::VietnameseEngine {
public:
  bool canProcessKey(uint32_t) const override { return true; }
  areca::BambooResult process(uint32_t, const std::string &text) override {
    areca::BambooResult result;
    result.currentText = current_;
    if (!current_.empty() && current_.back() == 'a' && text == "a") {
      result.deleteCount = 1;
      result.commitText = "\xC3\xA2";
      current_.back() = '?';
    } else {
      result.commitText = text;
      current_ += text;
    }
    return result;
  }
  areca::BambooResult processBackspace() override { return {}; }
  void backspace() override {}
  void reset() override { current_.clear(); }
  const std::string &currentText() const override { return current_; }

private:
  std::string current_;
};

class RecordingBackend final : public areca::RewriteBackend {
public:
  RecordingBackend(areca::MarkerDelivery delivery, TestInputContext &context)
      : delivery_(delivery), context_(context) {}

  const char *name() const override { return "recording"; }
  areca::MarkerDelivery markerDelivery() const override { return delivery_; }
  areca::ApplyStatus apply(areca::im::InputContext &inputContext,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone) override {
    applyTimesUsec.push_back(areca::im::now(CLOCK_MONOTONIC));
    context_.events.push_back("delete:" + std::to_string(plan.backspaceCount));
    inputContext.commitString(plan.commitText);
    if (onApply) {
      onApply();
    }
    return areca::ApplyStatus::Completed;
  }

  std::vector<uint64_t> applyTimesUsec;
  std::function<void()> onApply;

private:
  areca::MarkerDelivery delivery_;
  TestInputContext &context_;
};

struct Harness {
  explicit Harness(areca::MarkerDelivery delivery, uint32_t markerWaitMs = 30,
                   uint32_t probeTimeoutMs = 80)
      : context(manager), backend(delivery, context),
        scheduler(
            loop, [this](areca::im::InputContext &) { return &engine; },
            [markerWaitMs, probeTimeoutMs](areca::im::InputContext &) {
              areca::SchedulerTiming timing;
              timing.postCommitDelayMs = 0;
              timing.afterMarkerCommitWaitMs = markerWaitMs;
              timing.surroundingProbeTimeoutMs = probeTimeoutMs;
              return timing;
            },
            [] { return false; },
            [this](areca::im::InputContext &, const areca::BambooResult &) {
              return areca::RewriteBackendSelection{&backend};
            }) {
    scheduler.setCommitObserver([this](areca::im::InputContext &,
                                       uint32_t deleteCount,
                                       const std::string &text) {
      observed.push_back(std::to_string(deleteCount) + ":" + text);
    });
  }

  areca::im::EventLoop loop;
  areca::im::InputContextManager manager;
  TestInputContext context;
  TestEngine engine;
  RecordingBackend backend;
  areca::InputScheduler scheduler;
  std::vector<std::string> observed;
};

void runLoopFor(areca::im::EventLoop &loop, uint64_t usec) {
  auto timer =
      loop.addTimeEvent(CLOCK_MONOTONIC, areca::im::now(CLOCK_MONOTONIC) + usec,
                        0, [&loop](areca::im::EventSourceTime *, uint64_t) {
                          loop.exit();
                          return false;
                        });
  timer->setOneShot();
  loop.exec();
}

void testNoMarkerDeletesExactly() {
  Harness h(areca::MarkerDelivery::None);
  h.scheduler.enqueue(h.context, 'a', "a");
  runLoopFor(h.loop, 2000);
  h.context.events.clear();
  h.observed.clear();

  h.scheduler.enqueue(h.context, 'a', "a");
  // Surrounding xoá như trước khi có marker: không commit "·", xoá đúng N.
  assert((h.context.events ==
          std::vector<std::string>{"delete:1", "commit:\xC3\xA2"}));
  assert((h.observed == std::vector<std::string>{"1:\xC3\xA2"}));
}

void testKeyDeliveryWaitsForMarker() {
  Harness h(areca::MarkerDelivery::Keys, 30);
  h.scheduler.enqueue(h.context, 'a', "a");
  runLoopFor(h.loop, 2000);
  h.context.events.clear();
  h.observed.clear();

  const uint64_t startUsec = areca::im::now(CLOCK_MONOTONIC);
  h.scheduler.enqueue(h.context, 'a', "a");
  // Marker đã vào ô nhưng phím xoá phải chờ.
  assert((h.context.events == std::vector<std::string>{"commit:" + kMarker}));
  assert(h.backend.applyTimesUsec.empty());
  assert(h.scheduler.rewritePending());
  assert(h.scheduler.shouldRejectReset());

  // Phím đến trong lúc chờ nằm sau rewrite.
  h.scheduler.enqueue(h.context, 'b', "b");
  h.backend.onApply = [&h] { h.loop.exit(); };
  h.loop.exec();
  assert(h.backend.applyTimesUsec.size() == 1);
  assert(h.backend.applyTimesUsec.front() - startUsec >= 30000);
  runLoopFor(h.loop, 2000);
  assert((h.context.events ==
          std::vector<std::string>{"commit:" + kMarker, "delete:2",
                                   "commit:\xC3\xA2", "commit:b"}));
  assert((h.observed == std::vector<std::string>{"1:\xC3\xA2", "0:b"}));
}

void testKeyDeliveryWithoutWaitStillRunsAfterPost() {
  Harness h(areca::MarkerDelivery::Keys, 0);
  h.scheduler.enqueue(h.context, 'a', "a");
  runLoopFor(h.loop, 2000);
  h.context.events.clear();

  h.scheduler.enqueue(h.context, 'a', "a");
  assert(h.backend.applyTimesUsec.empty());
  runLoopFor(h.loop, 2000);
  assert((h.context.events == std::vector<std::string>{"commit:" + kMarker,
                                                       "delete:2",
                                                       "commit:\xC3\xA2"}));
}

void testContextLostDuringMarkerWait() {
  areca::im::EventLoop loop;
  areca::im::InputContextManager manager;
  auto context = std::make_unique<TestInputContext>(manager);
  TestEngine engine;
  RecordingBackend backend(areca::MarkerDelivery::Keys, *context);
  areca::InputScheduler scheduler(
      loop, [&](areca::im::InputContext &) { return &engine; },
      [](areca::im::InputContext &) {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 0;
        timing.afterMarkerCommitWaitMs = 5;
        return timing;
      },
      [] { return false; },
      [&](areca::im::InputContext &, const areca::BambooResult &) {
        return areca::RewriteBackendSelection{&backend};
      });
  int observedCount = 0;
  scheduler.setCommitObserver([&](areca::im::InputContext &, uint32_t,
                                  const std::string &) { ++observedCount; });
  scheduler.enqueue(*context, 'a', "a");
  runLoopFor(loop, 2000);
  observedCount = 0;

  scheduler.enqueue(*context, 'a', "a");
  assert(scheduler.rewritePending());
  context.reset();
  runLoopFor(loop, 20000);
  // Ô nhập mất thì kết thúc lượt, không xoá, không báo observer, không treo.
  assert(backend.applyTimesUsec.empty());
  assert(observedCount == 0);
  assert(!scheduler.rewritePending());
  assert(!scheduler.stalled());
}

// Ô có surrounding: "a" vừa gõ, gõ tiếp "a" để thành "â". Trả về thời điểm
// enqueue.
uint64_t startMarkerWithSurrounding(Harness &h) {
  h.context.setCapabilityFlags(areca::im::CapabilityFlag::SurroundingText);
  h.context.surroundingText().setText("", 0, 0);
  h.scheduler.enqueue(h.context, 'a', "a");
  runLoopFor(h.loop, 2000);
  h.context.surroundingText().setText("a", 1, 1);
  h.context.events.clear();
  h.observed.clear();
  const uint64_t startUsec = areca::im::now(CLOCK_MONOTONIC);
  h.scheduler.enqueue(h.context, 'a', "a");
  return startUsec;
}

void reportSurrounding(Harness &h, const std::string &text) {
  const auto length = static_cast<unsigned int>(areca::im::utf8::length(text));
  h.context.surroundingText().setText(text, length, length);
  h.scheduler.notifySurroundingTextUpdated(h.context);
}

// Chromium bỏ qua lệnh xoá tới trước khi nó báo marker: đợi update có marker
// rồi mới xoá, update cũ không đủ.
// Backend phím vẫn đợi marker, nhưng ứng dụng báo marker sớm thì phát phím
// luôn.
void testKeysStartEarlyOnMarkerEcho() {
  Harness h(areca::MarkerDelivery::Keys, 500, 80);
  const uint64_t startUsec = startMarkerWithSurrounding(h);
  reportSurrounding(h, "a" + kMarker);
  runLoopFor(h.loop, 5000);
  assert(h.backend.applyTimesUsec.size() == 1);
  assert(h.backend.applyTimesUsec.front() - startUsec < 400000);
}

} // namespace

int main() {
  testNoMarkerDeletesExactly();
  testKeyDeliveryWaitsForMarker();
  testKeyDeliveryWithoutWaitStillRunsAfterPost();
  testContextLostDuringMarkerWait();
  testKeysStartEarlyOnMarkerEcho();
  std::cout << "Rewrite marker test passed successfully\n";
  return 0;
}
