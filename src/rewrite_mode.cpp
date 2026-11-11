#include "rewrite_mode.h"

#include <exception>
#include <string>
#include <utility>

#include "keys.h"
#include "log.h"

namespace areca {
namespace {

std::string codepointToUtf8(uint32_t codepoint) {
  std::string out;
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  return out;
}

} // namespace

RewriteMode::RewriteMode(VietnameseEngine &engine, InputScheduler &scheduler,
                         DebugProvider debugProvider)
    : engine_(engine), scheduler_(scheduler),
      debugProvider_(std::move(debugProvider)) {}

KeyDisposition RewriteMode::handleKey(const KeyInput &key) {
  const bool release = key.state & keys::kReleaseMask;
  const bool isBackspace = key.keyval == keys::kBackSpace;
  if (debugProvider_() && !release) {
    ARECA_INFO() << "areca: rewrite handler keyval=0x" << std::hex << key.keyval
                 << std::dec << " state=" << key.state
                 << " pending=" << scheduler_.rewritePending()
                 << " queue=" << scheduler_.queuedKeyCount();
  }
  if (release || keys::isModifier(key.keyval)) {
    return KeyDisposition::Forward;
  }
  // Backspace do backend forward đi qua IBus sẽ không quay lại engine, nhưng
  // Backspace thật của người dùng lúc đang rewrite phải đi tiếp mà không đụng
  // trạng thái Bamboo.
  if (isBackspace && scheduler_.rewritePending()) {
    return KeyDisposition::Forward;
  }
  if (passwordField_) {
    requestReset();
    return KeyDisposition::Forward;
  }

  const bool resetAndForward =
      keys::isCursorMove(key.keyval) || key.keyval == keys::kTab ||
      key.keyval == keys::kKpTab || key.keyval == keys::kIsoLeftTab ||
      key.keyval == keys::kEscape || key.keyval == keys::kReturn ||
      key.keyval == keys::kKpEnter || (key.state & keys::kShortcutMask);
  if (resetAndForward) {
    // Phím do chính Areca vừa gây ra hoặc phím đến khi hàng đợi còn việc thì
    // reset tự bị từ chối, nên không làm rơi phím đang chờ.
    scheduler_.reset();
    return KeyDisposition::Forward;
  }
  if (key.keyval == keys::kDelete || key.keyval == keys::kKpDelete) {
    return KeyDisposition::Forward;
  }
  if (isBackspace) {
    if (scheduler_.shouldRejectReset()) {
      return KeyDisposition::Forward;
    }
    // Ứng dụng tự xoá ký tự cuối; Bamboo lùi theo để từ đang gõ vẫn khớp.
    try {
      engine_.backspace();
    } catch (const std::exception &error) {
      ARECA_ERROR() << "areca: rewrite Backspace failed: " << error.what();
      engine_.reset();
    }
    return KeyDisposition::Forward;
  }

  if (!key.codepoint || key.codepoint < 0x20 || key.codepoint == 0x7F) {
    return KeyDisposition::Forward;
  }
  scheduler_.enqueue(key.codepoint, codepointToUtf8(key.codepoint));
  return KeyDisposition::Accept;
}

void RewriteMode::requestReset() { scheduler_.reset(); }

} // namespace areca
