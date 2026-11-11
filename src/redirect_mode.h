#pragma once

#include "mode_handler.h"

namespace areca {

class RedirectModeHandler final : public InputModeHandler {
public:
  void activate(im::InputContext &) override {}
  void deactivate(im::InputContext &) override {}
  void handleKeyEvent(im::KeyEvent &event) override;
  void requestProtectedReset(im::InputContext &) override {}
  void resetContext(im::InputContext &) override {}
};

} // namespace areca
