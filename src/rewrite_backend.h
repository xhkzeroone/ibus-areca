#pragma once

#include <functional>

#include "im/im.h"

#include "types.h"

namespace areca {

enum class ApplyStatus {
  Completed,
  Pending,
  Failed,
};

enum class RewriteOutcome {
  Succeeded,
  Failed,
};

// Marker đến ứng dụng qua kênh nào so với lệnh xoá của backend.
enum class MarkerDelivery {
  // Không dùng marker: backend xoá đúng phần Bamboo yêu cầu (surrounding).
  None,
  // Xoá bằng phím đi qua kênh khác commit; phải chờ marker tới ứng dụng rồi
  // mới phát phím, nếu không Backspace có thể tới trước marker.
  Keys,
};

using RewriteDone =
    std::function<void(uint64_t transactionId, RewriteOutcome outcome)>;

class RewriteBackend {
public:
  virtual ~RewriteBackend() = default;
  virtual const char *name() const = 0;
  virtual MarkerDelivery markerDelivery() const { return MarkerDelivery::Keys; }
  virtual ApplyStatus apply(im::InputContext &inputContext,
                            const RewritePlan &plan, RewriteDone onDone) = 0;
};

} // namespace areca
