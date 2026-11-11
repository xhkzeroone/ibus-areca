#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "im/im.h"

#include "bamboo_engine_adapter.h"
#include "key_queue.h"
#include "rewrite_backend.h"

namespace areca {

struct SchedulerTiming {
  uint32_t backspaceDelayMs = 1;
  uint32_t uinputBackspaceAckTimeoutMs = 150;
  uint32_t afterBackspaceWaitMs = 10;

  uint32_t uinputShiftSelectDelayMs = 1;
  uint32_t uinputShiftSelectAckTimeoutMs = 150;
  uint32_t afterUinputShiftSelectWaitMs = 10;

  uint32_t surroundingWaitMs = 3;
  uint32_t surroundingProbeTimeoutMs = 80;
  uint32_t surroundingDeleteDelayMs = 10;
  uint32_t afterSurroundingDeleteWaitMs = 1;
  uint32_t xtestBackspaceDelayMs = 1;
  uint32_t afterXTestBackspaceWaitMs = 10;
  uint32_t postCommitDelayMs = 20;
  uint64_t timerAccuracyUsec = 1;
};

struct RewriteBackendSelection {
  RewriteBackend *backend = nullptr;
  uint32_t additionalBackspaces = 0;
  bool probeSurrounding = false;
  // 0 giữ timing chung. Probe tự động đặt 150 ms cho lần đầu và 80 ms cho lần
  // retry ở từ kế tiếp.
  uint32_t surroundingProbeTimeoutMs = 0;
};

class InputScheduler {
public:
  using EngineResolver = std::function<VietnameseEngine *(im::InputContext &)>;
  using TimingProvider = std::function<SchedulerTiming(im::InputContext &)>;
  using DebugProvider = std::function<bool()>;
  using RewriteBackendSelector = std::function<RewriteBackendSelection(
      im::InputContext &, const BambooResult &)>;
  // Báo mỗi lần text trong ứng dụng chắc chắn đã đổi do Areca: xoá
  // `deleteCount` ký tự trước con trỏ rồi chèn `text`.
  using CommitObserver = std::function<void(
      im::InputContext &, uint32_t deleteCount, const std::string &text)>;

  InputScheduler(im::EventLoop &eventLoop, EngineResolver engineResolver,
                 TimingProvider timingProvider, DebugProvider debugProvider,
                 RewriteBackendSelector rewriteBackendSelector);

  void enqueue(im::InputContext &inputContext, uint32_t codepoint,
               std::string utf8Text, bool suppressMacro = false);
  // Cho editor web nhận phím đầu qua frontend thay vì commitString để mở chế độ
  // soạn thảo. Vẫn xử lý Bamboo một lần để giữ trạng thái ghép dấu cho phím
  // sau. false: chưa sửa engine/event, caller có thể enqueue theo luồng thông
  // thường. true: đã xử lý xong hoặc bắt đầu applyResult, caller không enqueue
  // lần nữa.
  bool handleIdleKey(im::KeyEvent &event, uint32_t codepoint,
                     const std::string &utf8Text);
  void enqueueBackspace(im::InputContext &inputContext);
  void resetContext(im::InputContext &inputContext);
  void setCommitObserver(CommitObserver observer) {
    commitObserver_ = std::move(observer);
  }

  size_t queuedKeyCount() const { return queue_.size(); }
  bool rewritePending() const { return activeTransactionId_ != 0; }
  bool stalled() const { return stalled_; }
  bool shouldRejectReset() const;

private:
  // Trình duyệt thường gửi reset vài chục ms sau khi chữ trong ô đổi. Reset đó
  // lọt qua sẽ xoá từ đang gõ dở và bắt restore đọc lại snapshot có thể còn
  // trễ, gây lặp chữ.
  static constexpr uint64_t kResetRejectWindowUsec = 50 * 1000;

  void scheduleNext();
  void processNext();
  void applyResult(im::InputContext &inputContext, VietnameseEngine &engine,
                   const BambooResult &result, const std::string &rawText);
  void finishKey();
  void finishKeyAfterDelay();
  void finishKeyAfterCommit();
  void rewriteDone(uint64_t transactionId, RewriteOutcome outcome);
  void notifyPendingCommit();

  im::EventLoop &eventLoop_;
  EngineResolver engineResolver_;
  TimingProvider timingProvider_;
  DebugProvider debugProvider_;
  RewriteBackendSelector rewriteBackendSelector_;
  CommitObserver commitObserver_;
  // Kết quả của rewrite đang chờ backend báo xong, để báo observer khi thành
  // công.
  im::TrackableObjectReference<im::InputContext> pendingCommitContext_;
  uint32_t pendingCommitDeleteCount_ = 0;
  std::string pendingCommitText_;
  KeyQueue queue_;
  std::unique_ptr<im::EventSourceTime> postCommitTimer_;
  SchedulerTiming activeTiming_;
  uint64_t nextSequence_ = 1;
  uint64_t nextTransactionId_ = 1;
  uint64_t activeTransactionId_ = 0;
  uint64_t lastRewriteCompletionTimeUsec_ = 0;
  bool processing_ = false;
  bool stalled_ = false;
};

} // namespace areca
