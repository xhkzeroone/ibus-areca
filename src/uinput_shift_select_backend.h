#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "im/im.h"

#include "adaptive_wait.h"
#include "event_loop_post.h"
#include "rewrite_backend.h"
#include "uinput_device.h"

namespace areca {

class UinputShiftSelectBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  UinputShiftSelectBackend(im::EventLoop &eventLoop, UinputDevice &device,
                           AdaptiveWait &adaptiveWait,
                           DebugProvider debugProvider);
  UinputShiftSelectBackend(im::EventLoop &eventLoop, UinputDevice &device,
                           DebugProvider debugProvider);
  ~UinputShiftSelectBackend() override;

  const char *name() const override { return "uinput-shift-select"; }
  ApplyStatus apply(im::InputContext &inputContext, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool isAvailable();
  bool hasPending() const { return transactionId_ != 0; }
  bool handleKeyEvent(im::KeyEvent &event);

private:
  enum class TimerDispatch { TimerCallback, PostEvent };

  void beginSelection();
  void sendNextSelectionLeft();
  void releaseShiftAndWaitForAck();
  void releaseShift();
  void commitSelectionAndComplete();
  void scheduleCommit();
  void completeWithoutCommit();
  void finishTransaction();
  void schedule(uint32_t delayMs, TimerDispatch dispatch,
                std::function<void()> callback);
  void clearPending();

  im::EventLoop &eventLoop_;
  UinputDevice &device_;
  EventLoopPostTask commitPost_;
  AdaptiveWait *adaptiveWait_ = nullptr;
  DebugProvider debugProvider_;

  std::unique_ptr<im::EventSourceTime> timer_;
  im::TrackableObjectReference<im::InputContext> inputContext_;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t selectionCount_ = 0;
  uint32_t selectedCharacters_ = 0;
  uint32_t shiftSelectDelayMs_ = 0;
  uint32_t ackTimeoutMs_ = 150;
  uint32_t afterSelectWaitMs_ = 0;
  uint64_t timerAccuracyUsec_ = 1;
  uint64_t ackSentAtUsec_ = 0;
  bool shiftHeld_ = false;
  bool awaitingShiftUpAck_ = false;
  std::string commitText_;
};

} // namespace areca
