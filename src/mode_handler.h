#pragma once

#include "im/im.h"

namespace areca {

// Lifecycle contract only. Implementations own independent state and queues.
class InputModeHandler {
public:
  virtual ~InputModeHandler() = default;

  virtual void activate(im::InputContext &inputContext) = 0;
  virtual void deactivate(im::InputContext &inputContext) = 0;
  virtual void handleKeyEvent(im::KeyEvent &event) = 0;
  virtual void requestProtectedReset(im::InputContext &inputContext) = 0;
  virtual void resetContext(im::InputContext &inputContext) = 0;
};

} // namespace areca
