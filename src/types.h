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

// Thông số thời gian dùng chung cho scheduler và mọi backend. Một nơi khai báo
// để mặc định của scheduler và của plan không lệch nhau.
struct RewriteTiming {
  uint32_t backspaceDelayMs = 1;
  uint32_t uinputBackspaceAckTimeoutMs = 150;
  uint32_t afterBackspaceWaitMs = 10;

  uint32_t uinputShiftSelectDelayMs = 1;
  uint32_t uinputShiftSelectAckTimeoutMs = 150;
  uint32_t afterUinputShiftSelectWaitMs = 5;

  uint32_t surroundingWaitMs = 3;
  uint32_t surroundingProbeTimeoutMs = 80;
  uint32_t nativeBackspaceDelayMs = 1;
  uint32_t afterNativeBackspaceWaitMs = 10;
  uint32_t postCommitDelayMs = 20;
  uint64_t timerAccuracyUsec = 1;
  // Chờ marker tới ứng dụng trước khi backend phát phím xoá.
  uint32_t afterMarkerCommitWaitMs = 10;
};

struct RewritePlan : RewriteTiming {
  uint64_t transactionId = 0;
  uint32_t backspaceCount = 0;
  std::string commitText;
  bool probeSurrounding = false;
};

} // namespace areca
