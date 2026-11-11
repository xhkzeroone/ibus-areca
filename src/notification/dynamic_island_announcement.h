#pragma once

#include "input_method_announcement.h"

namespace areca {

class DynamicIslandAnnouncement final : public InputMethodAnnouncement {
public:
  explicit DynamicIslandAnnouncement(InputMethodIdentity identity);

protected:
  void announce(const InputMethodIdentity &identity) override;
};

} // namespace areca
