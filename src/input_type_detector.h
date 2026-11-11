#pragma once

#include <string>

namespace areca::im {
class InputContext;
}

namespace areca {

class WindowFocusTracker;

class InputTypeDetector {
public:
  explicit InputTypeDetector(const WindowFocusTracker *tracker = nullptr);

  void setFocusTracker(const WindowFocusTracker *tracker);

  bool isTerminal(const std::string &program) const;
  bool isBrowser(const std::string &program) const;
  bool isChromiumBrowser(const std::string &program) const;
  bool canForwardBackspaces(const im::InputContext &inputContext,
                            const std::string &program = "") const;

  bool inChromiumAddressBar(const im::InputContext &inputContext,
                            const std::string &program = "") const;

private:
  const WindowFocusTracker *tracker_ = nullptr;
};

} // namespace areca
