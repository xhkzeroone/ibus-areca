#pragma once

#include <cstdint>
#include <string>

namespace areca {

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
  uint32_t surroundingDeleteDelayMs = 10;
  uint32_t afterSurroundingDeleteWaitMs = 1;
  uint32_t xtestBackspaceDelayMs = 1;
  uint32_t afterXTestBackspaceWaitMs = 10;

  uint64_t timerAccuracyUsec = 1;
  std::string commitText;
  bool probeSurrounding = false;
};

} // namespace areca
