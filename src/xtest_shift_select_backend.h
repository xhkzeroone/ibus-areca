#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "im/im.h"

#include "adaptive_wait.h"
#include "event_loop_post.h"
#include "native_key_device.h"
#include "rewrite_backend.h"
#include "types.h"

namespace areca {

class XTestShiftSelectBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  XTestShiftSelectBackend(im::EventLoop &eventLoop, NativeKeyDevice &device,
                          AdaptiveWait &adaptiveWait,
                          DebugProvider debugProvider);
  ~XTestShiftSelectBackend() override;

  const char *name() const override { return "native-shift-select"; }
  ApplyStatus apply(im::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable() { return device_.isAvailable(); }
  bool hasPending() const { return transactionId_ != 0; }

private:
  enum class TimerDispatch { TimerCallback, PostEvent };

  bool beginSelection(bool notifyFailure);
  bool sendNextSelectionLeft(bool notifyFailure);
  void releaseShiftThenCommit();
  void releaseShift();
  void commitSelectionAndComplete();
  void scheduleCommit();
  void completeWithoutCommit();
  void failTransaction(bool notifyFailure);
  void finishTransaction();
  void schedule(uint32_t delayMs, TimerDispatch dispatch,
                std::function<void()> callback);
  void clearPending();

  im::EventLoop &eventLoop_;
  NativeKeyDevice &device_;
  EventLoopPostTask commitPost_;
  AdaptiveWait &adaptiveWait_;
  DebugProvider debugProvider_;

  std::unique_ptr<im::EventSourceTime> timer_;
  im::TrackableObjectReference<im::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t selectedCharacters_ = 0;
  uint32_t remainingLefts_ = 0;
  uint32_t shiftSelectDelayMs_ = 0;
  uint32_t afterSelectWaitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  bool shiftHeld_ = false;
  std::string commitText_;
};

} // namespace areca
