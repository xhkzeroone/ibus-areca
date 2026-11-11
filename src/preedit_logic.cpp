#include "preedit_logic.h"

#include "im/im.h"

namespace areca {

std::string buildPreeditCommit(const BambooResult &result) {
  if (!im::utf8::validate(result.currentText)) {
    return result.commitText;
  }
  const auto length = im::utf8::length(result.currentText);
  const auto keep =
      length > result.deleteCount ? length - result.deleteCount : 0;
  auto end = im::utf8::nextNChar(result.currentText.begin(), keep);
  return std::string(result.currentText.begin(), end) + result.commitText;
}

} // namespace areca
