#include "forward_backspace_backend.h"

#include <utility>

#include "log.h"

namespace areca {

ForwardBackspaceBackend::ForwardBackspaceBackend(DebugProvider debugProvider)
    : debugProvider_(std::move(debugProvider)) {}

ForwardBackspaceBackend::~ForwardBackspaceBackend() { clearPending(); }

ApplyStatus ForwardBackspaceBackend::apply(InputClient &client,
                                           const RewritePlan &plan,
                                           RewriteDone onDone) {
  if (hasPending() || !plan.transactionId) {
    return ApplyStatus::Failed;
  }

  // Lưu state trước khi gửi phím vì client có thể gọi lại đồng bộ.
  transactionId_ = plan.transactionId;
  client_ = &client;
  onDone_ = std::move(onDone);
  remainingBackspaces_ = plan.backspaceCount;
  sentBackspaces_ = 0;
  backspaceDelayMs_ = plan.backspaceDelayMs;
  afterBackspaceWaitMs_ = plan.afterBackspaceWaitMs;
  commitText_ = plan.commitText;

  if (debugProvider_()) {
    ARECA_INFO() << "areca: forward-backspace start tx=" << transactionId_
                 << " backspaces=" << remainingBackspaces_
                 << " delay_ms=" << backspaceDelayMs_
                 << " after_wait_ms=" << afterBackspaceWaitMs_;
  }

  if (!remainingBackspaces_) {
    timer_.start(afterBackspaceWaitMs_, [this]() { commitAndComplete(); });
  } else {
    sendNextBackspace();
  }
  return ApplyStatus::Pending;
}

void ForwardBackspaceBackend::sendNextBackspace() {
  client_->forwardBackspace();
  --remainingBackspaces_;
  ++sentBackspaces_;

  if (debugProvider_()) {
    ARECA_INFO() << "areca: forward-backspace sent tx=" << transactionId_
                 << " seen=" << sentBackspaces_
                 << " remaining=" << remainingBackspaces_;
  }

  if (remainingBackspaces_) {
    timer_.start(backspaceDelayMs_, [this]() { sendNextBackspace(); });
  } else {
    timer_.start(afterBackspaceWaitMs_, [this]() { commitAndComplete(); });
  }
}

void ForwardBackspaceBackend::commitAndComplete() {
  if (!commitText_.empty()) {
    client_->commitText(commitText_);
  }

  const uint64_t transactionId = transactionId_;
  auto onDone = std::move(onDone_);
  if (debugProvider_()) {
    ARECA_INFO() << "areca: forward-backspace complete tx=" << transactionId
                 << " sent=" << sentBackspaces_ << " commit=" << commitText_;
  }
  clearPending();
  if (onDone) {
    onDone(transactionId, RewriteOutcome::Succeeded);
  }
}

void ForwardBackspaceBackend::clearPending() {
  timer_.cancel();
  client_ = nullptr;
  onDone_ = {};
  transactionId_ = 0;
  remainingBackspaces_ = 0;
  sentBackspaces_ = 0;
  backspaceDelayMs_ = 0;
  afterBackspaceWaitMs_ = 0;
  commitText_.clear();
}

} // namespace areca
