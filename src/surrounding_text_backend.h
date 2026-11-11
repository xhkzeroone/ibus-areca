#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "im/im.h"

#include "adaptive_wait.h"
#include "event_loop_post.h"
#include "rewrite_backend.h"

namespace areca {

class SurroundingTextBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;
  using ProbeSucceededHandler = std::function<void(im::InputContext &)>;
  using ProbeFailedHandler = std::function<void(im::InputContext &)>;
  using FallbackBackendProvider =
      std::function<RewriteBackend *(im::InputContext &)>;

  SurroundingTextBackend(
      im::EventLoop &eventLoop, DebugProvider debugProvider,
      ProbeSucceededHandler onProbeSucceeded = nullptr,
      ProbeFailedHandler onProbeFailed = nullptr,
      FallbackBackendProvider fallbackBackendProvider = nullptr,
      AdaptiveWait *adaptiveWait = nullptr);
  ~SurroundingTextBackend() override;

  const char *name() const override { return "surrounding-text"; }
  ApplyStatus apply(im::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool hasPending() const { return transactionId_ != 0; }
  bool isProbing() const { return isProbe_; }

  // Gọi từ ArecaEngine khi nhận InputContextSurroundingTextUpdated trong lúc
  // probe.
  void notifySurroundingTextUpdated(im::InputContext &inputContext);

private:
  enum class TimerDispatch { TimerCallback, PostEvent };

  void scheduleCommit();
  void commitAfterAdaptiveWait(uint32_t appliedExtraWaitMs);
  void commitAndComplete();
  void completeWithoutCommit();
  void schedule(uint32_t delayMs, TimerDispatch dispatch,
                std::function<void()> callback);
  void dispatchPostEvent(uint64_t deadlineUsec, std::function<void()> callback);
  void observeAndRun(uint64_t deadlineUsec, std::function<void()> callback);
  void scheduleProbeDeadline();
  void handleProbeFailure();
  void clearPending();

  im::EventLoop &eventLoop_;
  EventLoopPostTask commitPost_;
  AdaptiveWait *adaptiveWait_ = nullptr;
  DebugProvider debugProvider_;
  ProbeSucceededHandler onProbeSucceeded_;
  ProbeFailedHandler onProbeFailed_;
  FallbackBackendProvider fallbackBackendProvider_;
  std::unique_ptr<im::EventSourceTime> timer_;
  im::TrackableObjectReference<im::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t waitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  std::string commitText_;

  bool isProbe_ = false;
  RewritePlan savedPlan_;
  std::string originalProbeText_;
  unsigned int originalProbeCursor_ = 0;
  std::string expectedProbeText_;
  unsigned int expectedProbeCursor_ = 0;
  unsigned int expectedProbeAnchor_ = 0;
  uint32_t probeUpdateCount_ = 0;
};

} // namespace areca
