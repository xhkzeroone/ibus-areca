#include "input_scheduler.h"

#include <exception>
#include <utility>

#include "im/im.h"

#include "surrounding_probe_match.h"
#include "surrounding_text_cache.h"

namespace areca {

InputScheduler::InputScheduler(im::EventLoop &eventLoop,
                               EngineResolver engineResolver,
                               TimingProvider timingProvider,
                               DebugProvider debugProvider,
                               RewriteBackendSelector rewriteBackendSelector)
    : eventLoop_(eventLoop), engineResolver_(std::move(engineResolver)),
      timingProvider_(std::move(timingProvider)),
      debugProvider_(std::move(debugProvider)),
      rewriteBackendSelector_(std::move(rewriteBackendSelector)),
      markerWaitPost_(eventLoop) {}

void InputScheduler::enqueue(im::InputContext &inputContext, uint32_t codepoint,
                             std::string utf8Text, bool suppressMacro) {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.codepoint = codepoint;
  key.utf8Text = std::move(utf8Text);
  key.suppressMacro = suppressMacro;
  key.inputContext = inputContext.watch();
  if (debugProvider_()) {
    ARECA_INFO() << "areca: queue push kind=text seq=" << key.sequence
                 << " text=" << key.utf8Text
                 << " suppress_macro=" << key.suppressMacro
                 << " depth_before=" << queue_.size();
  }
  queue_.push(std::move(key));
  scheduleNext();
}

bool InputScheduler::handleIdleKey(im::KeyEvent &event, uint32_t codepoint,
                                   const std::string &utf8Text) {
  auto *inputContext = event.inputContext();
  // Không cho phím vượt qua công việc đang chờ hoặc khoảng ổn định sau commit:
  // editor phải nhận đủ kết quả rewrite trước khi nhận phím mới. Khi stalled,
  // trạng thái editor chưa chắc đồng bộ nên cũng không được đi đường tắt này.
  if (!inputContext || processing_ || rewritePending() || !queue_.empty() ||
      stalled_ || shouldRejectReset()) {
    return false;
  }
  auto *engine = engineResolver_(*inputContext);
  // Chỉ áp dụng lúc chưa có từ đang ghép; phím trong từ vẫn cần luồng rewrite.
  if (!engine || !engine->currentText().empty())
    return false;

  // Khóa scheduler trong lúc xử lý đồng bộ để tránh công việc khác chen vào.
  processing_ = true;
  activeTiming_ = timingProvider_(*inputContext);
  try {
    // Engine rỗng chưa đảm bảo đầu ra giống phím gốc (bảng mã/macro có thể đổi
    // ký tự). Phải xem kết quả Bamboo trước khi quyết định bỏ filterAndAccept.
    const auto result = engine->process(codepoint, utf8Text);
    if (result.deleteCount == 0 && result.commitText == utf8Text &&
        !result.macroExpanded) {
      if (debugProvider_())
        ARECA_INFO() << "areca: idle key route=forward reason=unchanged-output";
      // Giữ trạng thái mới của Bamboo để a rồi s vẫn ghép thành á. Không commit
      // thêm hoặc tự cập nhật surrounding cache vì frontend sẽ giao phím gốc.
      // forward() chỉ là getter; chính việc không filter/accept event mới
      // cho phím đi tiếp. Không tạo cặp phím giả bằng forwardKey ở đây.
      event.forward();
      if (commitObserver_) {
        commitObserver_(*inputContext, 0, utf8Text);
      }
      // Dù không gọi commitString(), frontend vẫn sắp nhận một ký tự mới từ
      // phím gốc. Giữ single-flight barrier như nhánh commit để phím kế tiếp
      // không bắt đầu rewrite trước khi editor kịp nhận ký tự vừa forward.
      finishKeyAfterDelay();
    } else {
      if (debugProvider_())
        ARECA_INFO()
            << "areca: idle key route=commit reason=transformed-output";
      // Đầu ra đã đổi: chặn phím gốc và áp dụng chính kết quả vừa tính. Không
      // enqueue lại vì như vậy Bamboo sẽ xử lý cùng một phím hai lần.
      event.filterAndAccept();
      applyResult(*inputContext, *engine, result, utf8Text);
    }
  } catch (const std::exception &error) {
    ARECA_ERROR() << "areca: idle key processing failed: " << error.what();
    engine->reset();
    event.forward();
    finishKey();
  }
  return true;
}

void InputScheduler::enqueueBackspace(im::InputContext &inputContext) {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.isBackspace = true;
  key.inputContext = inputContext.watch();
  if (debugProvider_()) {
    ARECA_INFO() << "areca: queue push kind=backspace seq=" << key.sequence
                 << " depth_before=" << queue_.size();
  }
  queue_.push(std::move(key));
  scheduleNext();
}

void InputScheduler::enqueueReplacement(im::InputContext &inputContext,
                                        std::string text,
                                        ReplacedTextResolver replaced) {
  QueuedKey key;
  key.sequence = nextSequence_++;
  key.utf8Text = std::move(text);
  key.replacedText = std::move(replaced);
  key.inputContext = inputContext.watch();
  if (debugProvider_()) {
    ARECA_INFO() << "areca: queue push kind=replace seq=" << key.sequence
                 << " text=" << key.utf8Text
                 << " depth_before=" << queue_.size();
  }
  queue_.push(std::move(key));
  scheduleNext();
}

bool InputScheduler::shouldRejectReset() const {
  if (processing_ || rewritePending()) {
    return true;
  }
  if (lastRewriteCompletionTimeUsec_ != 0) {
    const uint64_t now = im::now(CLOCK_MONOTONIC);
    if (now >= lastRewriteCompletionTimeUsec_ &&
        (now - lastRewriteCompletionTimeUsec_) <= kResetRejectWindowUsec) {
      return true;
    }
  }
  return false;
}

void InputScheduler::resetContext(im::InputContext &inputContext) {
  if (shouldRejectReset()) {
    if (debugProvider_()) {
      ARECA_INFO() << "areca: reset rejected (active rewrite or 50ms "
                      "post-commit window)";
    }
    return;
  }

  queue_.removeFor(inputContext);
  if (auto *engine = engineResolver_(inputContext)) {
    engine->reset();
  }
  scheduleNext();
}

void InputScheduler::scheduleNext() {
  if (processing_ || queue_.empty() || stalled_) {
    return;
  }
  if (debugProvider_()) {
    ARECA_INFO() << "areca: scheduler pump depth=" << queue_.size();
  }
  processNext();
}

void InputScheduler::processNext() {
  if (processing_ || queue_.empty() || stalled_) {
    return;
  }

  auto key = queue_.pop();
  auto *inputContext = key.inputContext.get();
  if (!inputContext) {
    scheduleNext();
    return;
  }
  auto *engine = engineResolver_(*inputContext);
  if (!engine) {
    scheduleNext();
    return;
  }

  processing_ = true;
  if (debugProvider_()) {
    ARECA_INFO() << "areca: scheduler process seq=" << key.sequence << " kind="
                 << (key.replacedText  ? "replace"
                     : key.isBackspace ? "backspace"
                                       : "text")
                 << " depth_after=" << queue_.size();
  }
  try {
    if (key.replacedText) {
      const std::string replaced = key.replacedText(*inputContext);
      const auto length = im::utf8::lengthValidated(replaced);
      if (replaced.empty() || length == im::utf8::INVALID_LENGTH) {
        finishKey();
        return;
      }
      // Từ đang ghép là một phần đoạn bị thay; reset để phím sau bắt đầu từ
      // mới thay vì sửa tiếp chữ đã mất.
      engine->reset();
      BambooResult result;
      result.currentText = replaced;
      result.deleteCount = static_cast<uint32_t>(length);
      result.commitText = key.utf8Text;
      applyResult(*inputContext, *engine, result, key.utf8Text);
    } else if (key.isBackspace) {
      applyResult(*inputContext, *engine, engine->processBackspace(), "");
    } else {
      const auto result =
          key.suppressMacro
              ? engine->processWithoutMacro(key.codepoint, key.utf8Text)
              : engine->process(key.codepoint, key.utf8Text);
      applyResult(*inputContext, *engine, result, key.utf8Text);
    }
  } catch (const std::exception &error) {
    ARECA_ERROR() << "areca: Bamboo processing failed: " << error.what();
    engine->reset();
    finishKey();
  }
}

void InputScheduler::applyResult(im::InputContext &inputContext,
                                 VietnameseEngine &engine,
                                 const BambooResult &result,
                                 const std::string &rawText) {
  activeTiming_ = timingProvider_(inputContext);
  if (debugProvider_()) {
    ARECA_INFO() << "areca: bamboo result current=" << result.currentText
                 << " new=" << result.newText
                 << " delete=" << result.deleteCount
                 << " commit=" << result.commitText
                 << " macro=" << result.macroExpanded
                 << " spellcheck_restored=" << result.spellcheckRestored;
  }
  if (!result.deleteCount) {
    // The original event was accepted before queueing, so every no-delete
    // result is applied through the input-method protocol. This also covers
    // output tables that transform a single key without replacing old text.
    if (!result.commitText.empty()) {
      inputContext.commitString(result.commitText);
      updateSurroundingCacheAfterCommit(inputContext, result.commitText);
      if (commitObserver_) {
        commitObserver_(inputContext, 0, result.commitText);
      }
    }
    if (debugProvider_()) {
      ARECA_INFO() << "areca: apply no-delete commit=" << result.commitText
                   << " raw=" << rawText
                   << " transformed=" << (result.commitText != rawText);
    }
    finishKeyAfterCommit();
    return;
  }

  const auto &timing = activeTiming_;
  RewritePlan plan;
  plan.transactionId = nextTransactionId_++;
  static_cast<RewriteTiming &>(plan) = timing;
  plan.commitText = result.commitText;

  const auto selection = rewriteBackendSelector_(inputContext, result);
  if (!selection.backend) {
    ARECA_ERROR() << "areca: rewrite backend selector returned null";
    engine.reset();
    activeTransactionId_ = 0;
    finishKey();
    return;
  }
  auto &backend = *selection.backend;
  // Xoá thêm marker "·" sắp commit cùng với chữ cũ.
  plan.backspaceCount = result.deleteCount + 1;
  plan.probeSurrounding = selection.probeSurrounding;
  if (selection.surroundingProbeTimeoutMs > 0) {
    plan.surroundingProbeTimeoutMs = selection.surroundingProbeTimeoutMs;
  }
  if (debugProvider_()) {
    ARECA_INFO() << "areca: rewrite select backend=" << backend.name()
                 << " tx=" << plan.transactionId
                 << " bamboo_delete=" << result.deleteCount
                 << " probe_timeout_ms=" << plan.surroundingProbeTimeoutMs
                 << " plan_backspaces=" << plan.backspaceCount;
  }

  activeTransactionId_ = plan.transactionId;
  pendingCommitContext_ = inputContext.watch();
  pendingCommitDeleteCount_ = result.deleteCount;
  pendingCommitText_ = result.commitText;

  // Marker không báo commit observer: xét cả lượt, chữ trong ô vẫn là xoá
  // deleteCount ký tự rồi chèn chữ mới. Cache surrounding phải có marker để
  // lệnh xoá N+1 ký tự tính đúng vị trí.
  const std::string marker(kRewriteMarker);
  // Chỉ ứng dụng có surrounding mới báo lại được rằng marker đã vào ô.
  const bool canEcho = inputContext.capabilityFlags().test(
                           im::CapabilityFlag::SurroundingText) &&
                       inputContext.surroundingText().isValid();
  inputContext.commitString(marker);
  updateSurroundingCacheAfterCommit(inputContext, marker);

  // Chromium bỏ qua lệnh xoá tới liền sau commit, khi nó chưa kịp báo lại trạng
  // thái mới: marker còn nguyên và probe hết giờ. Vì vậy đợi ứng dụng báo
  // surrounding có marker rồi mới xoá; ứng dụng không báo thì xoá khi hết giờ.
  // Phím xoá đi kênh khác commit cũng phải đợi, kẻo tới trước marker và xoá
  // nhầm một ký tự thật.
  const bool keys = backend.markerDelivery() == MarkerDelivery::Keys;
  const uint32_t waitMs = keys      ? activeTiming_.afterMarkerCommitWaitMs
                          : canEcho ? activeTiming_.surroundingProbeTimeoutMs
                                    : 0;
  if (debugProvider_()) {
    ARECA_INFO() << "areca: rewrite marker tx=" << plan.transactionId
                 << " backend=" << backend.name() << " wait_ms=" << waitMs
                 << " wait_echo=" << canEcho;
  }
  if (!keys && waitMs == 0) {
    startRewrite(inputContext, backend, plan);
    return;
  }

  markerContext_ = inputContext.watch();
  markerBackend_ = &backend;
  markerPlan_ = plan;
  markerAwaitsEcho_ = canEcho;
  if (waitMs == 0) {
    markerWaitPost_.schedule([this]() { proceedAfterMarker("no-wait"); });
    return;
  }
  markerWaitTimer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC,
      im::now(CLOCK_MONOTONIC) + static_cast<uint64_t>(waitMs) * 1000,
      activeTiming_.timerAccuracyUsec, [this](im::EventSourceTime *, uint64_t) {
        auto completedTimer = std::move(markerWaitTimer_);
        // Phát phím ở pha post để event đang chờ được dispatch trước.
        markerWaitPost_.schedule([this]() { proceedAfterMarker("timeout"); });
        return false;
      });
  if (!markerWaitTimer_) {
    markerWaitPost_.schedule([this]() { proceedAfterMarker("no-timer"); });
    return;
  }
  markerWaitTimer_->setOneShot();
}

void InputScheduler::notifySurroundingTextUpdated(
    im::InputContext &inputContext) {
  if (!markerBackend_ || !markerAwaitsEcho_ ||
      markerContext_.get() != &inputContext) {
    return;
  }
  const auto &surrounding = inputContext.surroundingText();
  std::string_view before;
  if (!surrounding.isValid() || surrounding.cursor() != surrounding.anchor() ||
      !textBeforeCursor(surrounding.text(), surrounding.cursor(), before) ||
      !before.ends_with(kRewriteMarker)) {
    // Update cũ từ trước khi marker vào ô.
    return;
  }
  markerAwaitsEcho_ = false;
  markerWaitTimer_.reset();
  markerWaitPost_.schedule([this]() { proceedAfterMarker("echo"); });
}

void InputScheduler::proceedAfterMarker(const char *reason) {
  auto *backend = std::exchange(markerBackend_, nullptr);
  auto *inputContext = markerContext_.get();
  markerContext_.unwatch();
  markerAwaitsEcho_ = false;
  markerWaitTimer_.reset();
  if (!backend) {
    return;
  }
  if (debugProvider_()) {
    ARECA_INFO() << "areca: rewrite marker settled tx="
                 << markerPlan_.transactionId << " reason=" << reason;
  }
  if (!inputContext) {
    // Ô nhập đã mất: không còn gì để xoá hay commit.
    activeTransactionId_ = 0;
    pendingCommitContext_.unwatch();
    pendingCommitDeleteCount_ = 0;
    pendingCommitText_.clear();
    finishKey();
    return;
  }
  startRewrite(*inputContext, *backend, markerPlan_);
}

void InputScheduler::startRewrite(im::InputContext &inputContext,
                                  RewriteBackend &backend,
                                  const RewritePlan &plan) {
  const auto status =
      backend.apply(inputContext, plan,
                    [this](uint64_t transactionId, RewriteOutcome outcome) {
                      rewriteDone(transactionId, outcome);
                    });
  if (debugProvider_()) {
    ARECA_INFO() << "areca: rewrite apply backend=" << backend.name()
                 << " tx=" << plan.transactionId
                 << " status=" << static_cast<int>(status);
  }
  if (status == ApplyStatus::Completed) {
    activeTransactionId_ = 0;
    notifyPendingCommit();
    finishKeyAfterCommit();
  } else if (status == ApplyStatus::Failed) {
    ARECA_ERROR() << "areca: rewrite backend failed tx=" << plan.transactionId
                  << " backend=" << backend.name();
    // Fail closed because a backend failure leaves the application state
    // unknown. Processing another key could violate ordering.
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
  // Start the settle delay from the backend's actual completion barrier.
  finishKeyAfterCommit();
}

void InputScheduler::notifyPendingCommit() {
  auto *inputContext = pendingCommitContext_.get();
  pendingCommitContext_.unwatch();
  if (inputContext && commitObserver_) {
    commitObserver_(*inputContext, pendingCommitDeleteCount_,
                    pendingCommitText_);
  }
  pendingCommitDeleteCount_ = 0;
  pendingCommitText_.clear();
}

void InputScheduler::finishKey() {
  processing_ = false;
  scheduleNext();
}

void InputScheduler::finishKeyAfterCommit() {
  lastRewriteCompletionTimeUsec_ = im::now(CLOCK_MONOTONIC);
  finishKeyAfterDelay();
}

void InputScheduler::finishKeyAfterDelay() {
  const uint64_t delayUsec =
      static_cast<uint64_t>(activeTiming_.postCommitDelayMs) * 1000;
  if (debugProvider_()) {
    ARECA_INFO() << "areca: post-commit barrier delay_us=" << delayUsec
                 << " accuracy_us=" << activeTiming_.timerAccuracyUsec
                 << " queue_depth=" << queue_.size();
  }
  if (delayUsec == 0) {
    finishKey();
    return;
  }

  const uint64_t deadline = im::now(CLOCK_MONOTONIC) + delayUsec;
  postCommitTimer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, deadline, activeTiming_.timerAccuracyUsec,
      [this](im::EventSourceTime *, uint64_t) {
        auto completedTimer = std::move(postCommitTimer_);
        finishKey();
        return false;
      });
  if (!postCommitTimer_) {
    finishKey();
    return;
  }
  postCommitTimer_->setOneShot();
}

} // namespace areca
