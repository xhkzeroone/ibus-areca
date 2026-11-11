#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "bamboo_engine_adapter.h"
#include "input_client.h"
#include "one_shot_timer.h"
#include "rewrite_backend.h"

namespace areca {

struct SchedulerTiming {
  uint32_t backspaceDelayMs = 1;
  uint32_t afterBackspaceWaitMs = 10;
  uint32_t postCommitDelayMs = 20;
};

struct QueuedKey {
  uint64_t sequence = 0;
  uint32_t codepoint = 0;
  std::string utf8Text;
  bool isBackspace = false;
};

// Hàng đợi FIFO single-flight của chế độ Rewrite: một phím xử lý xong, kể cả
// barrier sau commit, mới tới phím sau. Backend lỗi làm scheduler dừng hẳn vì
// trạng thái ô nhập lúc đó không còn biết chắc.
class InputScheduler final {
public:
  using TimingProvider = std::function<SchedulerTiming()>;
  using DebugProvider = std::function<bool()>;
  using RewriteBackendSelector =
      std::function<RewriteBackend *(const BambooResult &)>;
  // Báo mỗi lần text trong ứng dụng chắc chắn đã đổi do Areca: xoá
  // `deleteCount` ký tự trước con trỏ rồi chèn `text`.
  using CommitObserver =
      std::function<void(uint32_t deleteCount, const std::string &text)>;

  InputScheduler(VietnameseEngine &engine, InputClient &client,
                 TimingProvider timingProvider, DebugProvider debugProvider,
                 RewriteBackendSelector rewriteBackendSelector);
  InputScheduler(const InputScheduler &) = delete;
  InputScheduler &operator=(const InputScheduler &) = delete;

  void enqueue(uint32_t codepoint, std::string utf8Text);
  void enqueueBackspace();
  // Bỏ các phím đang chờ và reset Bamboo, trừ khi đang trong cửa sổ chặn reset.
  // Trả về false khi reset bị từ chối.
  bool reset();
  void setCommitObserver(CommitObserver observer) {
    commitObserver_ = std::move(observer);
  }

  size_t queuedKeyCount() const { return queue_.size(); }
  bool rewritePending() const { return activeTransactionId_ != 0; }
  bool stalled() const { return stalled_; }
  bool shouldRejectReset() const;

private:
  // Trình duyệt thường gửi reset vài chục ms sau khi chữ trong ô đổi. Reset đó
  // lọt qua sẽ xoá từ đang gõ dở, phím kế tiếp mất dấu.
  static constexpr uint64_t kResetRejectWindowUsec = 50 * 1000;

  void scheduleNext();
  void processNext();
  void applyResult(const BambooResult &result, const std::string &rawText);
  void finishKey();
  void finishKeyAfterCommit();
  void rewriteDone(uint64_t transactionId, RewriteOutcome outcome);
  void notifyPendingCommit();

  VietnameseEngine &engine_;
  InputClient &client_;
  TimingProvider timingProvider_;
  DebugProvider debugProvider_;
  RewriteBackendSelector rewriteBackendSelector_;
  CommitObserver commitObserver_;
  // Kết quả của rewrite đang chờ backend báo xong, để báo observer khi thành
  // công.
  uint32_t pendingCommitDeleteCount_ = 0;
  std::string pendingCommitText_;
  std::deque<QueuedKey> queue_;
  OneShotTimer postCommitTimer_;
  SchedulerTiming activeTiming_;
  uint64_t nextSequence_ = 1;
  uint64_t nextTransactionId_ = 1;
  uint64_t activeTransactionId_ = 0;
  uint64_t lastRewriteCompletionTimeUsec_ = 0;
  bool processing_ = false;
  bool stalled_ = false;
};

} // namespace areca
