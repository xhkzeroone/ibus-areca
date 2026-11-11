#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "im/im.h"

#include "input_scheduler.h"

namespace {

class TestInputContext final : public areca::im::InputContext {
public:
  explicit TestInputContext(areca::im::InputContextManager &manager)
      : areca::im::InputContext(manager, "google-chrome") {}
  ~TestInputContext() override { destroy(); }

  const char *frontend() const override { return "wayland"; }

  void commitStringImpl(const std::string &text) override {
    events.push_back("commit:" + text);
    commitTimesUsec.push_back(areca::im::now(CLOCK_MONOTONIC));
    if (onCommit)
      onCommit();
  }
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &event) override {
    events.push_back(std::string(event.isRelease() ? "up:" : "down:") +
                     areca::im::Key::keySymToUTF8(event.rawKey().sym()));
  }
  void updatePreeditImpl() override {}

  std::vector<std::string> events;
  std::vector<uint64_t> commitTimesUsec;
  std::function<void()> onCommit;
};

class TestEngine final : public areca::VietnameseEngine {
public:
  bool canProcessKey(uint32_t) const override { return true; }

  areca::BambooResult process(uint32_t, const std::string &text) override {
    areca::BambooResult result;
    result.currentText = current_;
    result.newText = current_ + text;
    result.deleteCount = text == "r" ? 1 : 0;
    result.commitText = text == "x" ? "X" : text;
    current_ = result.newText;
    return result;
  }

  areca::BambooResult processBackspace() override { return {}; }
  void backspace() override {}
  void reset() override { current_.clear(); }
  const std::string &currentText() const override { return current_; }

private:
  std::string current_;
};

class PendingBackend final : public areca::RewriteBackend {
public:
  const char *name() const override { return "pending-test"; }

  areca::ApplyStatus apply(areca::im::InputContext &,
                           const areca::RewritePlan &plan,
                           areca::RewriteDone onDone) override {
    transactionId_ = plan.transactionId;
    onDone_ = std::move(onDone);
    return areca::ApplyStatus::Pending;
  }

  void complete() {
    assert(onDone_);
    auto onDone = std::move(onDone_);
    onDone(transactionId_, areca::RewriteOutcome::Succeeded);
  }

  void fail() {
    assert(onDone_);
    auto onDone = std::move(onDone_);
    onDone(transactionId_, areca::RewriteOutcome::Failed);
  }

private:
  uint64_t transactionId_ = 0;
  areca::RewriteDone onDone_;
};

} // namespace

int main() {
  areca::im::EventLoop eventLoop;
  areca::im::InputContextManager manager;
  TestInputContext inputContext(manager);
  TestEngine engine;
  PendingBackend backend;

  areca::InputScheduler scheduler(
      eventLoop,
      [&](areca::im::InputContext &) -> areca::VietnameseEngine * {
        return &engine;
      },
      [](areca::im::InputContext &) {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 0;
        return timing;
      },
      [] { return false; },
      [&](areca::im::InputContext &, const areca::BambooResult &result) {
        return areca::RewriteBackendSelection{result.deleteCount ? &backend
                                                                 : nullptr};
      });
  // Observer nhận đúng phần text ứng dụng vừa đổi do Areca để mode handler
  // biết chắc text trước con trỏ.
  std::vector<std::string> observed;
  scheduler.setCommitObserver([&](areca::im::InputContext &, uint32_t deleteCount,
                                  const std::string &text) {
    observed.push_back(std::to_string(deleteCount) + ":" + text);
  });

  // Phím đầu không bị nhận/chèn lặp, nhưng Bamboo phải nhớ nó để ghép dấu.
  areca::im::KeyEvent first(&inputContext, areca::im::Key(areca::im::Key_a));
  assert(scheduler.handleIdleKey(first, 'a', "a"));
  assert(!first.accepted());
  assert(observed == std::vector<std::string>{"0:a"});
  assert(inputContext.events.empty());
  assert(engine.currentText() == "a");

  // Đã có từ đang ghép: từ chối đường tắt mà không sửa trạng thái engine.
  areca::im::KeyEvent next(&inputContext, areca::im::Key(areca::im::Key_s));
  assert(!scheduler.handleIdleKey(next, 's', "s"));
  assert(engine.currentText() == "a");

  engine.reset();
  // Engine đổi đầu ra thì phải chặn phím gốc và commit đúng một lần.
  areca::im::KeyEvent transformed(&inputContext, areca::im::Key(areca::im::Key_x));
  assert(scheduler.handleIdleKey(transformed, 'x', "x"));
  assert(transformed.accepted());
  assert(inputContext.events == std::vector<std::string>{"commit:X"});
  assert(observed.back() == "0:X");

  inputContext.events.clear();
  // Dù engine rỗng, rewrite/queue đang chờ vẫn phải giữ thứ tự giao phím.
  scheduler.enqueue(inputContext, 'r', "r");
  assert(scheduler.rewritePending());
  engine.reset();
  areca::im::KeyEvent pending(&inputContext, areca::im::Key(areca::im::Key_a));
  assert(!scheduler.handleIdleKey(pending, 'a', "a"));
  assert(engine.currentText().empty());
  scheduler.enqueue(inputContext, 'a', "a");
  assert(!scheduler.handleIdleKey(pending, 'a', "a"));
  const auto observedBeforeRewrite = observed.size();
  backend.complete();
  // Rewrite bất đồng bộ chỉ được báo khi backend xác nhận xong.
  assert(observed.size() > observedBeforeRewrite);
  assert(observed[observedBeforeRewrite] == "1:r");
  assert(inputContext.events == std::vector<std::string>{"commit:a"});

  // Một phím được forward trực tiếp vẫn phải giữ single-flight barrier. Phím
  // kế tiếp đến sớm nằm trong queue và chỉ được commit sau đủ 20 ms.
  areca::im::EventLoop barrierEventLoop;
  TestInputContext barrierInputContext(manager);
  TestEngine barrierEngine;
  PendingBackend barrierBackend;
  areca::InputScheduler barrierScheduler(
      barrierEventLoop,
      [&](areca::im::InputContext &) -> areca::VietnameseEngine * {
        return &barrierEngine;
      },
      [](areca::im::InputContext &) {
        areca::SchedulerTiming timing;
        timing.postCommitDelayMs = 20;
        timing.timerAccuracyUsec = 1;
        return timing;
      },
      [] { return false; },
      [&](areca::im::InputContext &, const areca::BambooResult &result) {
        return areca::RewriteBackendSelection{
            result.deleteCount ? &barrierBackend : nullptr};
      });

  const uint64_t forwardStartUsec = areca::im::now(CLOCK_MONOTONIC);
  areca::im::KeyEvent barrierFirst(&barrierInputContext, areca::im::Key(areca::im::Key_a));
  assert(barrierScheduler.handleIdleKey(barrierFirst, 'a', "a"));
  assert(!barrierFirst.accepted());

  areca::im::KeyEvent barrierSecond(&barrierInputContext, areca::im::Key(areca::im::Key_s));
  assert(!barrierScheduler.handleIdleKey(barrierSecond, 's', "s"));
  barrierScheduler.enqueue(barrierInputContext, 's', "s");
  assert(barrierScheduler.queuedKeyCount() == 1);
  assert(barrierInputContext.events.empty());

  barrierInputContext.onCommit = [&] { barrierEventLoop.exit(); };
  assert(barrierEventLoop.exec());
  assert(barrierInputContext.events == std::vector<std::string>{"commit:s"});
  assert(barrierInputContext.commitTimesUsec.size() == 1);
  assert(barrierInputContext.commitTimesUsec.front() - forwardStartUsec >=
         20000);

  // Lỗi bất đồng bộ không được mở khóa queue vì trạng thái editor đã không còn
  // xác định. Scheduler phải giữ fail-closed giống lỗi apply đồng bộ.
  areca::im::EventLoop failureEventLoop;
  TestInputContext failureInputContext(manager);
  TestEngine failureEngine;
  PendingBackend failureBackend;
  areca::InputScheduler failureScheduler(
      failureEventLoop,
      [&](areca::im::InputContext &) -> areca::VietnameseEngine * {
        return &failureEngine;
      },
      [](areca::im::InputContext &) { return areca::SchedulerTiming{}; },
      [] { return false; },
      [&](areca::im::InputContext &, const areca::BambooResult &result) {
        return areca::RewriteBackendSelection{
            result.deleteCount ? &failureBackend : nullptr};
      });

  failureScheduler.enqueue(failureInputContext, 'r', "r");
  assert(failureScheduler.rewritePending());
  failureBackend.fail();
  assert(failureScheduler.stalled());
  assert(failureScheduler.rewritePending());
}
