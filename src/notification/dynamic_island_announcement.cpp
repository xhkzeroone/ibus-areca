#include "dynamic_island_announcement.h"

#include <utility>

#include <gio/gio.h>

namespace areca {

DynamicIslandAnnouncement::DynamicIslandAnnouncement(
    InputMethodIdentity identity)
    : InputMethodAnnouncement(std::move(identity)) {}

void DynamicIslandAnnouncement::announce(const InputMethodIdentity &identity) {
  // Chỉ phát signal trên session bus đã có, không chờ Island trả lời.
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  if (!bus) {
    return;
  }
  g_dbus_connection_emit_signal(
      bus, nullptr, "/com/github/dynamic_island/InputMethod",
      "com.github.dynamic_island.InputMethod", "Activated",
      g_variant_new("(sss)", identity.id.c_str(), identity.name.c_str(),
                    identity.icon.c_str()),
      nullptr);
  g_object_unref(bus);
}

} // namespace areca
