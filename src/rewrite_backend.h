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

using RewriteDone =
    std::function<void(uint64_t transactionId, RewriteOutcome outcome)>;

class RewriteBackend {
public:
  virtual ~RewriteBackend() = default;
  virtual const char *name() const = 0;
  virtual ApplyStatus apply(im::InputContext &inputContext,
                            const RewritePlan &plan, RewriteDone onDone) = 0;
};

} // namespace areca
