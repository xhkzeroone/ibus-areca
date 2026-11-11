#include "input_scheduler.h"

#include <exception>
#include <utility>

#include "log.h"

namespace areca {

InputScheduler::InputScheduler(VietnameseEngine &engine, InputClient &client,
                               TimingProvider timingProvider,
                               DebugProvider debugProvider,
                               RewriteBackendSelector rewriteBackendSelector)
    : engine_(engine), client_(client),
      timingProvider_(std::move(timingProvider)),
      debugProvider_(std::move(debugProvider)),
      rewriteBackendSelector_(std::move(rewriteBackendSelector)) {}

void InputScheduler::enqueue(uint32_t codepoint, std::string utf8Text) {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.codepoint = codepoint;
  key.utf8Text = std::move(utf8Text);
  if (debugProvider_()) {
    ARECA_INFO() << "areca: queue push kind=text seq=" << key.sequence
                 << " text=" << key.utf8Text
                 << " depth_before=" << queue_.size();
  }
  queue_.push_back(std::move(key));
  scheduleNext();
}

void InputScheduler::enqueueBackspace() {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.isBackspace = true;
  if (debugProvider_()) {
    ARECA_INFO() << "areca: queue push kind=backspace seq=" << key.sequence
                 << " depth_before=" << queue_.size();
  }
  queue_.push_back(std::move(key));
  scheduleNext();
}

bool InputScheduler::shouldRejectReset() const {
  if (processing_ || rewritePending()) {
    return true;
  }
  if (lastRewriteCompletionTimeUsec_ != 0) {
    const uint64_t now = monotonicNowUsec();
    if (now >= lastRewriteCompletionTimeUsec_ &&
        (now - lastRewriteCompletionTimeUsec_) <= kResetRejectWindowUsec) {
      return true;
    }
  }
  return false;
}

bool InputScheduler::reset() {
  if (shouldRejectReset()) {
    if (debugProvider_()) {
      ARECA_INFO() << "areca: reset rejected (active rewrite or 50ms "
                      "post-commit window)";
    }
    return false;
  }
  queue_.clear();
  engine_.reset();
  return true;
}

void InputScheduler::scheduleNext() {
  if (processing_ || queue_.empty() || stalled_) {
    return;
  }
  processNext();
}

void InputScheduler::processNext() {
  auto key = std::move(queue_.front());
  queue_.pop_front();

  processing_ = true;
  if (debugProvider_()) {
    ARECA_INFO() << "areca: scheduler process seq=" << key.sequence
                 << " kind=" << (key.isBackspace ? "backspace" : "text")
                 << " depth_after=" << queue_.size();
  }
  try {
    if (key.isBackspace) {
      applyResult(engine_.processBackspace(), "");
    } else {
      applyResult(engine_.process(key.codepoint, key.utf8Text), key.utf8Text);
    }
  } catch (const std::exception &error) {
    ARECA_ERROR() << "areca: Bamboo processing failed: " << error.what();
    engine_.reset();
    finishKey();
  }
}

void InputScheduler::applyResult(const BambooResult &result,
                                 const std::string &rawText) {
  activeTiming_ = timingProvider_();
  if (debugProvider_()) {
    ARECA_INFO() << "areca: bamboo result current=" << result.currentText
                 << " new=" << result.newText
                 << " delete=" << result.deleteCount
                 << " commit=" << result.commitText
                 << " macro=" << result.macroExpanded
                 << " spellcheck_restored=" << result.spellcheckRestored;
  }
  if (!result.deleteCount) {
    // Phím gốc đã được nhận trước khi vào hàng đợi, nên mọi kết quả không cần
    // xoá đều đi qua commit, kể cả khi bảng mã đổi ký tự.
    if (!result.commitText.empty()) {
      client_.commitText(result.commitText);
      if (commitObserver_) {
        commitObserver_(0, result.commitText);
      }
    }
    if (debugProvider_()) {
      ARECA_INFO() << "areca: apply no-delete commit=" << result.commitText
                   << " raw=" << rawText;
    }
    finishKeyAfterCommit();
    return;
  }

  RewritePlan plan;
  plan.transactionId = nextTransactionId_++;
  plan.backspaceCount = result.deleteCount;
  plan.backspaceDelayMs = activeTiming_.backspaceDelayMs;
  plan.afterBackspaceWaitMs = activeTiming_.afterBackspaceWaitMs;
  plan.commitText = result.commitText;

  auto *backend = rewriteBackendSelector_(result);
  if (!backend) {
    ARECA_ERROR() << "areca: rewrite backend selector returned null";
    engine_.reset();
    finishKey();
    return;
  }
  if (debugProvider_()) {
    ARECA_INFO() << "areca: rewrite select backend=" << backend->name()
                 << " tx=" << plan.transactionId
                 << " bamboo_delete=" << result.deleteCount
                 << " plan_backspaces=" << plan.backspaceCount;
  }

  activeTransactionId_ = plan.transactionId;
  pendingCommitDeleteCount_ = result.deleteCount;
  pendingCommitText_ = result.commitText;
  const auto status = backend->apply(
      client_, plan, [this](uint64_t transactionId, RewriteOutcome outcome) {
        rewriteDone(transactionId, outcome);
      });
  if (debugProvider_()) {
    ARECA_INFO() << "areca: rewrite apply backend=" << backend->name()
                 << " tx=" << plan.transactionId
                 << " status=" << static_cast<int>(status);
  }
  if (status == ApplyStatus::Completed) {
    activeTransactionId_ = 0;
    notifyPendingCommit();
    finishKeyAfterCommit();
  } else if (status == ApplyStatus::Failed) {
    ARECA_ERROR() << "areca: rewrite backend failed tx=" << plan.transactionId
                  << " backend=" << backend->name();
    // Dừng hẳn: xử lý phím tiếp theo khi không biết ô nhập đang chứa gì có thể
    // phá thứ tự phím.
    stalled_ = true;
  }
}

void InputScheduler::rewriteDone(uint64_t transactionId,
                                 RewriteOutcome outcome) {
  if (debugProvider_()) {
    ARECA_INFO() << "areca: rewrite done tx=" << transactionId
                 << " active=" << activeTransactionId_
                 << " success=" << (outcome == RewriteOutcome::Succeeded);
  }
  if (!processing_ || transactionId != activeTransactionId_) {
    return;
  }
  if (outcome == RewriteOutcome::Failed) {
    ARECA_ERROR() << "areca: asynchronous rewrite backend failure tx="
                  << transactionId;
    stalled_ = true;
    return;
  }
  activeTransactionId_ = 0;
  notifyPendingCommit();
  finishKeyAfterCommit();
}

void InputScheduler::notifyPendingCommit() {
  if (commitObserver_) {
    commitObserver_(pendingCommitDeleteCount_, pendingCommitText_);
  }
  pendingCommitDeleteCount_ = 0;
  pendingCommitText_.clear();
}

void InputScheduler::finishKey() {
  processing_ = false;
  scheduleNext();
}

void InputScheduler::finishKeyAfterCommit() {
  lastRewriteCompletionTimeUsec_ = monotonicNowUsec();
  const uint32_t delayMs = activeTiming_.postCommitDelayMs;
  if (debugProvider_()) {
    ARECA_INFO() << "areca: post-commit barrier delay_ms=" << delayMs
                 << " queue_depth=" << queue_.size();
  }
  if (delayMs == 0) {
    finishKey();
    return;
  }
  postCommitTimer_.start(delayMs, [this]() { finishKey(); });
}

} // namespace areca
