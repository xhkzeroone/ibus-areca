#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "one_shot_timer.h"
#include "rewrite_backend.h"

namespace areca {

// Xoá bằng cách gửi Backspace qua IBus cho ứng dụng, chờ một nhịp rồi commit.
// Không có ACK: ứng dụng xử lý phím forward bất đồng bộ với commit, nên thời
// gian chờ sau Backspace cuối là thứ giữ đúng thứ tự.
class ForwardBackspaceBackend final : public RewriteBackend {
public:
  using DebugProvider = std::function<bool()>;

  explicit ForwardBackspaceBackend(DebugProvider debugProvider);
  ~ForwardBackspaceBackend() override;
  ForwardBackspaceBackend(const ForwardBackspaceBackend &) = delete;
  ForwardBackspaceBackend &operator=(const ForwardBackspaceBackend &) = delete;

  const char *name() const override { return "forward-backspace"; }
  ApplyStatus apply(InputClient &client, const RewritePlan &plan,
                    RewriteDone onDone) override;

  bool hasPending() const { return transactionId_ != 0; }

private:
  void sendNextBackspace();
  void commitAndComplete();
  void clearPending();

  DebugProvider debugProvider_;
  OneShotTimer timer_;
  InputClient *client_ = nullptr;
  RewriteDone onDone_;
  uint64_t transactionId_ = 0;
  uint32_t remainingBackspaces_ = 0;
  uint32_t sentBackspaces_ = 0;
  uint32_t backspaceDelayMs_ = 0;
  uint32_t afterBackspaceWaitMs_ = 0;
  std::string commitText_;
};

} // namespace areca
