#include "input_type_detector.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "im/im.h"

#include "program_compatibility.h"
#include "window_focus_tracker.h"

namespace areca {
namespace {

bool isSdlProgram(std::string program) {
  const auto slash = program.find_last_of("/\\");
  if (slash != std::string::npos) {
    program.erase(0, slash + 1);
  }
  std::transform(
      program.begin(), program.end(), program.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  constexpr std::string_view desktopSuffix = ".desktop";
  if (program.ends_with(desktopSuffix)) {
    program.resize(program.size() - desktopSuffix.size());
  }
  return program.starts_with("sdl");
}

} // namespace

InputTypeDetector::InputTypeDetector(const WindowFocusTracker *tracker)
    : tracker_(tracker) {}

void InputTypeDetector::setFocusTracker(const WindowFocusTracker *tracker) {
  tracker_ = tracker;
}

bool InputTypeDetector::isBrowser(const std::string &program) const {
  if (!program.empty()) {
    return isBrowserLikeProgram(program);
  }
  if (tracker_ && tracker_->isValid()) {
    const std::string tracked = tracker_->focusProgram();
    if (!tracked.empty()) {
      return isBrowserLikeProgram(tracked);
    }
  }
  return true;
}

bool InputTypeDetector::isChromiumBrowser(const std::string &program) const {
  if (!program.empty()) {
    return areca::isChromiumBrowser(program);
  }
  if (tracker_ && tracker_->isValid()) {
    const std::string tracked = tracker_->focusProgram();
    if (!tracked.empty()) {
      return areca::isChromiumBrowser(tracked);
    }
  }
  return false;
}

bool InputTypeDetector::isTerminal(const std::string &program) const {
  if (isTerminalProgram(program)) {
    return true;
  }
  if (tracker_ && tracker_->isValid() && tracker_->isTerminalFocused()) {
    if (!program.empty() && isBrowserLikeProgram(program)) {
      return false;
    }
    return true;
  }
  return false;
}

bool InputTypeDetector::canForwardBackspaces(
    const im::InputContext &inputContext, const std::string &program) const {
  std::string prog = !program.empty() ? program : inputContext.program();
  if (prog.empty() && tracker_ && tracker_->isValid()) {
    prog = tracker_->focusProgram();
  }

  // Ứng dụng SDL thường tự vẽ và tự xử lý phím, không nhận Backspace mà IBus
  // forward, nên phải xoá bằng phím phần cứng.
  return !isSdlProgram(prog);
}

bool InputTypeDetector::inChromiumAddressBar(
    const im::InputContext &inputContext, const std::string &program) const {
  // Url is authoritative. AT-SPI only fills the gap for Chromium frontends
  // that do not expose CapabilityFlag::Url.
  if (inputContext.capabilityFlags().test(im::CapabilityFlag::Url)) {
    return true;
  }
  if (!tracker_ || !tracker_->isValid())
    return false;
  const auto focus = tracker_->browserFocus();
  std::string prog = program.empty() ? inputContext.program() : program;
  if (prog.empty())
    prog = focus.program;
  return isChromiumBrowser(prog) && isChromiumBrowser(focus.program) &&
         focus.className.find("Omnibox") != std::string::npos;
}

} // namespace areca
