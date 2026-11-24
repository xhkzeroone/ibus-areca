#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "im/im.h"

#include "bamboo_engine_adapter.h"
#include "event_loop_post.h"
#include "key_queue.h"
#include "rewrite_backend.h"

namespace areca {

using SchedulerTiming = RewriteTiming;

struct RewriteBackendSelection {
  RewriteBackend *backend = nullptr;
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
  using ReplacedTextResolver = std::function<std::string(im::InputContext &)>;
  // Xoá đoạn text `replaced` trả về lúc tới lượt rồi chèn `text`, qua cùng
  // rewrite backend như phím thường. Trả về rỗng thì bỏ qua.
  void enqueueReplacement(im::InputContext &inputContext, std::string text,
                          ReplacedTextResolver replaced);
  void resetContext(im::InputContext &inputContext);
  // Ứng dụng báo surrounding mới: rewrite đang đợi marker vào ô thì bắt đầu
  // xoá.
  void notifySurroundingTextUpdated(im::InputContext &inputContext);
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
  void startRewrite(im::InputContext &inputContext, RewriteBackend &backend,
                    const RewritePlan &plan);
  void proceedAfterMarker(const char *reason);
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
  std::unique_ptr<im::EventSourceTime> markerWaitTimer_;
  EventLoopPostTask markerWaitPost_;
  // Rewrite đã commit marker, đang đợi nó tới ứng dụng rồi mới xoá.
  im::TrackableObjectReference<im::InputContext> markerContext_;
  RewriteBackend *markerBackend_ = nullptr;
  RewritePlan markerPlan_;
  bool markerAwaitsEcho_ = false;
  SchedulerTiming activeTiming_;
  uint64_t nextSequence_ = 1;
  uint64_t nextTransactionId_ = 1;
  uint64_t activeTransactionId_ = 0;
  uint64_t lastRewriteCompletionTimeUsec_ = 0;
  bool processing_ = false;
  bool stalled_ = false;
};

} // namespace areca
