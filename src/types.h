#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace areca {

// Commit trước mỗi lần xoá chữ trong Rewrite. Không gợi ý tự hoàn thành nào
// bắt đầu bằng ký tự này nên trình duyệt bỏ gợi ý hay vùng chọn ngay khi nó vào
// ô, nhờ vậy mọi backend xoá như ở ô thường. Marker bị xoá cùng chữ cũ.
inline constexpr std::string_view kRewriteMarker = "\xC2\xB7";

struct BambooResult {
  std::string currentText;
  std::string newText;
  uint32_t deleteCount = 0;
  std::string commitText;
  bool macroExpanded = false;
  bool spellcheckRestored = false;
};

struct RewritePlan {
  uint64_t transactionId = 0;
  uint32_t backspaceCount = 0;
  uint32_t backspaceDelayMs = 1;
  uint32_t uinputBackspaceAckTimeoutMs = 150;
  uint32_t afterBackspaceWaitMs = 10;

  uint32_t uinputShiftSelectDelayMs = 1;
  uint32_t uinputShiftSelectAckTimeoutMs = 150;
  uint32_t afterUinputShiftSelectWaitMs = 10;

  uint32_t surroundingWaitMs = 3;
  uint32_t surroundingProbeTimeoutMs = 80;
  uint32_t nativeBackspaceDelayMs = 1;
  uint32_t afterNativeBackspaceWaitMs = 10;

  uint64_t timerAccuracyUsec = 1;
  std::string commitText;
  bool probeSurrounding = false;
};

} // namespace areca
