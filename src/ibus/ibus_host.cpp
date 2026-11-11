#include "ibus/ibus_host.h"

#include <algorithm>

#include <gio/gio.h>

#include "ibus/ibus_input_context.h"

namespace areca {
namespace {

// Gộp nhiều lần ghi file liên tiếp của app cài đặt thành một lần nạp lại.
constexpr uint64_t kConfigReloadDelayUsec = 200 * 1000;

} // namespace

IbusHost &IbusHost::instance() {
  // Sống tới hết process: engine IBus có thể bị huỷ bất cứ lúc nào.
  static IbusHost *host = new IbusHost();
  return *host;
}

IbusHost::IbusHost() {
  engine_ = std::make_unique<ArecaEngine>(*this);
  engine_->userInterfaceManager().setUpdateHook(
      [this](im::SimpleAction *, im::InputContext *) {
        scheduleStatusRefresh();
      });
  watchConfigDirectory();
}

void IbusHost::foreachInputContext(
    const std::function<bool(im::InputContext *)> &fn) {
  // Chép ra trước: callback có thể làm context bị huỷ.
  const auto contexts = contexts_;
  for (auto *context : contexts) {
    if (!fn(context)) {
      break;
    }
  }
}

void IbusHost::showInputMethodInformation(im::InputContext *inputContext,
                                          const std::string &text) {
  auto *context = static_cast<IbusInputContext *>(inputContext);
  if (!context) {
    context = focusedContext_;
  }
  if (context) {
    context->showInformation(text);
  }
}

void IbusHost::updateStatusArea(im::InputContext *) { scheduleStatusRefresh(); }

void IbusHost::scheduleStatusRefresh() {
  // Một lần đổi cấu hình cập nhật hàng chục action; vẽ lại menu một lần.
  if (statusRefresh_) {
    return;
  }
  statusRefresh_ = eventLoop_.addDeferEvent([this](im::EventSource *) {
    auto source = std::move(statusRefresh_);
    if (focusedContext_) {
      focusedContext_->refreshStatusArea();
    }
    return false;
  });
}

void IbusHost::sendNotification(const std::string &summary,
                                const std::string &body, int timeoutMs) {
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  if (!bus) {
    return;
  }
  GVariantBuilder actions;
  g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
  GVariantBuilder hints;
  g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
  g_dbus_connection_call(
      bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
      "org.freedesktop.Notifications", "Notify",
      g_variant_new("(susssasa{sv}i)", "Areca", 0U, "ibus-areca",
                    summary.c_str(), body.c_str(), &actions, &hints, timeoutMs),
      nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
  g_object_unref(bus);
}

std::string IbusHost::configDirectory() const {
  gchar *path =
      g_build_filename(g_get_user_config_dir(), "ibus-areca", nullptr);
  std::string result(path);
  g_free(path);
  return result;
}

void IbusHost::addContext(IbusInputContext *context) {
  contexts_.push_back(context);
}

void IbusHost::removeContext(IbusInputContext *context) {
  contexts_.erase(std::remove(contexts_.begin(), contexts_.end(), context),
                  contexts_.end());
  if (focusedContext_ == context) {
    focusedContext_ = nullptr;
  }
}

void IbusHost::watchConfigDirectory() {
  const auto directory = configDirectory() + "/conf";
  g_mkdir_with_parents(directory.c_str(), 0700);
  GFile *file = g_file_new_for_path(directory.c_str());
  configMonitor_ = g_file_monitor_directory(file, G_FILE_MONITOR_WATCH_MOVES,
                                            nullptr, nullptr);
  g_object_unref(file);
  if (!configMonitor_) {
    return;
  }
  g_signal_connect(configMonitor_, "changed",
                   G_CALLBACK(+[](GFileMonitor *, GFile *, GFile *,
                                  GFileMonitorEvent event, gpointer data) {
                     if (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
                         event == G_FILE_MONITOR_EVENT_CREATED ||
                         event == G_FILE_MONITOR_EVENT_DELETED ||
                         event == G_FILE_MONITOR_EVENT_MOVED_IN ||
                         event == G_FILE_MONITOR_EVENT_RENAMED) {
                       static_cast<IbusHost *>(data)->scheduleConfigReload();
                     }
                   }),
                   this);
}

void IbusHost::scheduleConfigReload() {
  configReloadTimer_ = eventLoop_.addTimeEvent(
      CLOCK_MONOTONIC, im::now(CLOCK_MONOTONIC) + kConfigReloadDelayUsec, 0,
      [this](im::EventSourceTime *, uint64_t) {
        auto timer = std::move(configReloadTimer_);
        engine_->reloadConfig();
        return false;
      });
}

} // namespace areca
