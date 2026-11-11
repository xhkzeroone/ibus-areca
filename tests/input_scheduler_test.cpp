#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include "bamboo_engine_adapter.h"
#include "forward_backspace_backend.h"
#include "input_scheduler.h"
#include "test_support.h"

namespace {

using areca::ApplyStatus;
using areca::BambooEngineAdapter;
using areca::BambooResult;
using areca::ForwardBackspaceBackend;
using areca::InputClient;
using areca::InputScheduler;
using areca::RewriteBackend;
using areca::RewriteDone;
using areca::RewriteOutcome;
using areca::RewritePlan;
using areca::SchedulerTiming;
using areca::test::FakeClient;
using areca::test::runFor;
using areca::test::runUntil;

void typeAll(InputScheduler &scheduler, const std::string &keys) {
  for (char key : keys) {
    scheduler.enqueue(static_cast<unsigned char>(key), std::string(1, key));
  }
}

struct Fixture {
  explicit Fixture(SchedulerTiming timing = {})
      : timing(timing), backend([]() { return false; }),
        scheduler(
            engine, client, [this]() { return this->timing; },
            []() { return false; },
            [this](const BambooResult &) -> RewriteBackend * {
              return selected ? selected : &backend;
            }) {}

  SchedulerTiming timing;
  BambooEngineAdapter engine;
  FakeClient client;
  ForwardBackspaceBackend backend;
  RewriteBackend *selected = nullptr;
  InputScheduler scheduler;
};

// Backend giữ transaction để test tự báo xong hoặc lỗi.
class ManualBackend final : public RewriteBackend {
public:
  const char *name() const override { return "manual"; }
  ApplyStatus apply(InputClient &client, const RewritePlan &plan,
                    RewriteDone onDone) override {
    ++applyCount;
    lastPlan = plan;
    this->client = &client;
    done = std::move(onDone);
    return status;
  }

  ApplyStatus status = ApplyStatus::Pending;
  int applyCount = 0;
  RewritePlan lastPlan;
  InputClient *client = nullptr;
  RewriteDone done;
};

void testTypesSentenceInOrder() {
  Fixture fixture;
  // Đưa cả câu vào hàng đợi cùng lúc như người gõ nhanh.
  typeAll(fixture.scheduler, "gox ngon laf dduwojc");
  assert(runUntil([&]() {
    return fixture.scheduler.queuedKeyCount() == 0 &&
           !fixture.scheduler.rewritePending() &&
           !fixture.scheduler.shouldRejectReset();
  }));
  assert(fixture.client.text == "gõ ngon là được");
}

void testSingleFlightWaitsForBarrier() {
  Fixture fixture;
  typeAll(fixture.scheduler, "aw");
  // Phím đầu commit ngay, phím sau còn chờ barrier sau commit.
  assert(fixture.client.operations.size() == 1);
  assert(fixture.client.operations[0] == "commit:a");
  assert(fixture.scheduler.queuedKeyCount() == 1);

  const int64_t start = g_get_monotonic_time();
  assert(runUntil([&]() { return fixture.client.text == "ă"; }));
  assert(g_get_monotonic_time() - start >= 20 * 1000);
  // Backspace phải tới trước commit chữ mới.
  assert(fixture.client.operations.size() == 3);
  assert(fixture.client.operations[1] == "backspace");
  assert(fixture.client.operations[2] == "commit:ă");
}

void testResetRejectWindow() {
  Fixture fixture;
  typeAll(fixture.scheduler, "a");
  // Đang trong barrier sau commit: reset không được xoá từ đang gõ.
  assert(fixture.scheduler.shouldRejectReset());
  assert(!fixture.scheduler.reset());
  assert(fixture.engine.currentText() == "a");

  runFor(70);
  assert(!fixture.scheduler.shouldRejectReset());
  assert(fixture.scheduler.reset());
  assert(fixture.engine.currentText().empty());
}

void testFailedBackendStallsScheduler() {
  Fixture fixture;
  ManualBackend manual;
  manual.status = ApplyStatus::Failed;
  fixture.selected = &manual;
  typeAll(fixture.scheduler, "aw");
  assert(runUntil([&]() { return manual.applyCount == 1; }));
  assert(fixture.scheduler.stalled());

  typeAll(fixture.scheduler, "b");
  runFor(60);
  // Không phím nào được xử lý sau khi dừng.
  assert(fixture.scheduler.queuedKeyCount() == 1);
  assert(fixture.client.operations.size() == 1);
}

void testPendingBackendCompletesOnlyMatchingTransaction() {
  Fixture fixture;
  ManualBackend manual;
  fixture.selected = &manual;
  typeAll(fixture.scheduler, "awn");
  assert(runUntil([&]() { return manual.applyCount == 1; }));
  assert(fixture.scheduler.rewritePending());
  assert(manual.lastPlan.backspaceCount == 1);
  assert(manual.lastPlan.commitText == "ă");

  // Báo nhầm transaction thì bỏ qua, phím sau vẫn chờ.
  manual.done(manual.lastPlan.transactionId + 1, RewriteOutcome::Succeeded);
  assert(fixture.scheduler.rewritePending());
  runFor(40);
  assert(fixture.scheduler.queuedKeyCount() == 1);

  manual.client->forwardBackspace();
  manual.client->commitText("ă");
  manual.done(manual.lastPlan.transactionId, RewriteOutcome::Succeeded);
  assert(!fixture.scheduler.rewritePending());
  assert(runUntil([&]() { return fixture.client.text == "ăn"; }));
}

void testAsyncFailureStalls() {
  Fixture fixture;
  ManualBackend manual;
  fixture.selected = &manual;
  typeAll(fixture.scheduler, "awn");
  assert(runUntil([&]() { return manual.applyCount == 1; }));
  manual.done(manual.lastPlan.transactionId, RewriteOutcome::Failed);
  assert(fixture.scheduler.stalled());
  runFor(40);
  assert(fixture.scheduler.queuedKeyCount() == 1);
}

void testCommitObserverSeesEveryChange() {
  Fixture fixture;
  std::string known;
  fixture.scheduler.setCommitObserver(
      [&](uint32_t deleteCount, const std::string &text) {
        known += "-" + std::to_string(deleteCount) + "+" + text;
      });
  typeAll(fixture.scheduler, "aw");
  assert(runUntil([&]() { return fixture.client.text == "ă"; }));
  assert(runUntil([&]() { return known == "-0+a-1+ă"; }));
}

} // namespace

int main() {
  testTypesSentenceInOrder();
  testSingleFlightWaitsForBarrier();
  testResetRejectWindow();
  testFailedBackendStallsScheduler();
  testPendingBackendCompletesOnlyMatchingTransaction();
  testAsyncFailureStalls();
  testCommitObserverSeesEveryChange();
  std::cout << "input_scheduler_test passed\n";
  return 0;
}
