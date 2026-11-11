#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "input_client.h"

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

struct RewritePlan {
  uint64_t transactionId = 0;
  uint32_t backspaceCount = 0;
  uint32_t backspaceDelayMs = 1;
  uint32_t afterBackspaceWaitMs = 10;
  std::string commitText;
};

class RewriteBackend {
public:
  virtual ~RewriteBackend() = default;
  virtual const char *name() const = 0;
  virtual ApplyStatus apply(InputClient &client, const RewritePlan &plan,
                            RewriteDone onDone) = 0;
};

} // namespace areca
