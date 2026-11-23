#include "ibus/ibus_host.h"

#include <algorithm>

#include <gio/gio.h>

#include "ibus/ibus_emoji_loader.h"
#include "ibus/ibus_input_context.h"

namespace areca {
namespace {

// Gộp nhiều lần ghi file liên tiếp của app cài đặt thành một lần nạp lại.
constexpr uint64_t kConfigReloadDelayUsec = 200 * 1000;

// Chạy lệnh ngoài mà không chờ, để khỏi chặn event loop.
void spawnDetached(std::vector<std::string> args) {
  std::vector<char *> argv;
  for (auto &arg : args) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  g_spawn_async(nullptr, argv.data(), nullptr,
                static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH), nullptr, nullptr,
                nullptr, nullptr);
}

bool fileExists(const gchar *path) {
  return g_file_test(path, G_FILE_TEST_IS_REGULAR);
}

// Icon có trong theme của người dùng hoặc của hệ thống.
bool iconExists(const std::string &name) {
  const std::vector<std::string> relatives = {
      "icons/" + name + ".png",
      "icons/hicolor/256x256/apps/" + name + ".png",
      "icons/hicolor/scalable/apps/" + name + ".svg",
      "icons/hicolor/scalable/apps/" + name + ".png",
  };
  std::vector<std::string> roots = {g_get_user_data_dir()};
  for (const gchar *const *dir = g_get_system_data_dirs(); dir && *dir; ++dir) {
    roots.push_back(*dir);
  }
  for (const auto &root : roots) {
    for (const auto &relative : relatives) {
      gchar *path = g_build_filename(root.c_str(), relative.c_str(), nullptr);
      const bool found = fileExists(path);
      g_free(path);
      if (found) {
        return true;
      }
    }
  }
  return false;
}

std::string actionLabel(const im::SimpleAction &action, bool withValue) {
  if (withValue && !action.longText().empty()) {
    return action.shortText() + ": " + action.longText();
  }
  return action.shortText();
}

} // namespace

IbusHost &IbusHost::instance() {
  // Sống tới hết process: engine IBus có thể bị huỷ bất cứ lúc nào.
  static IbusHost *host = new IbusHost();
  return *host;
}

IbusHost::IbusHost() {
  engine_ = std::make_unique<ArecaEngine>(*this, loadIbusEmojiAnnotations);
  engine_->userInterfaceManager().setUpdateHook(
      [this](im::SimpleAction *, im::InputContext *) {
        scheduleStatusRefresh();
      });
  watchConfigDirectory();
  trayItem_ = std::make_unique<StatusNotifierItem>(
      []() { spawnDetached({ARECA_SETTINGS_PATH}); });
  trayItem_->setMenuProvider([this]() { return buildTrayMenu(); });
  syncTrayIcon();
}

std::vector<StatusNotifierItem::MenuItem> IbusHost::buildTrayMenu() {
  using MenuItem = StatusNotifierItem::MenuItem;
  std::vector<MenuItem> items;
  if (!arecaActive()) {
    MenuItem use;
    use.key = "use-areca";
    use.label = "Sử dụng Areca";
    use.activate = [this]() {
      if (engine_->debugLoggingEnabled()) {
        ARECA_INFO() << "areca: tray menu select areca engine";
      }
      if (bus_) {
        ibus_bus_set_global_engine_async(bus_, "areca", 5000, nullptr, nullptr,
                                         nullptr);
      } else {
        spawnDetached({"ibus", "engine", "areca"});
      }
    };
    items.push_back(std::move(use));
    MenuItem separator;
    separator.key = "separator-top";
    separator.separator = true;
    items.push_back(std::move(separator));
  }
  IbusInputContext *context = focusedContext_;
  if (!context && !contexts_.empty()) {
    context = contexts_.front();
  }
  if (context) {
    const auto contextWatch = context->watch();
    for (auto *action : context->statusArea().actions()) {
      MenuItem item;
      item.key = "action:" + action->name();
      item.label = actionLabel(*action, true);
      if (auto *menu = action->menu()) {
        for (auto *sub : menu->actions()) {
          MenuItem child;
          child.key = "action:" + sub->name();
          child.label = actionLabel(*sub, false);
          child.toggle = MenuItem::Toggle::Radio;
          child.checked = sub->isChecked();
          const std::string name = sub->name();
          child.activate = [contextWatch, name]() {
            if (auto *ic = contextWatch.get()) {
              static_cast<IbusInputContext *>(ic)->activateProperty(
                  name, PROP_STATE_CHECKED);
            }
          };
          item.children.push_back(std::move(child));
        }
      } else {
        if (action->isCheckable()) {
          item.toggle = MenuItem::Toggle::Check;
          item.checked = action->isChecked();
        }
        const std::string name = action->name();
        const bool checkable = action->isCheckable();
        const bool checked = action->isChecked();
        item.activate = [contextWatch, name, checkable, checked]() {
          if (auto *ic = contextWatch.get()) {
            static_cast<IbusInputContext *>(ic)->activateProperty(
                name, checkable && !checked ? PROP_STATE_CHECKED
                                            : PROP_STATE_UNCHECKED);
          }
        };
      }
      items.push_back(std::move(item));
    }
  }
  if (!items.empty() && !items.back().separator) {
    MenuItem separator;
    separator.key = "separator-bottom";
    separator.separator = true;
    items.push_back(std::move(separator));
  }
  MenuItem restart;
  restart.key = "restart-ibus";
  restart.label = "Khởi động lại IBus";
  restart.activate = [this]() {
    if (engine_->debugLoggingEnabled()) {
      ARECA_INFO() << "areca: tray menu restart ibus";
    }
    if (bus_) {
      ibus_bus_exit_async(bus_, TRUE, -1, nullptr, nullptr, nullptr);
    } else {
      spawnDetached({"ibus", "restart"});
    }
  };
  items.push_back(std::move(restart));
  return items;
}

bool IbusHost::arecaActive() const {
  if (globalEngineKnown_) {
    return globalEngineIsAreca_;
  }
  return !enabledContexts_.empty();
}

void IbusHost::setGlobalEngine(const std::string &name) {
  globalEngineKnown_ = true;
  globalEngineIsAreca_ = name == "areca";
  syncTrayIcon();
}

void IbusHost::engineEnabled(IbusInputContext *context) {
  if (std::find(enabledContexts_.begin(), enabledContexts_.end(), context) ==
      enabledContexts_.end()) {
    enabledContexts_.push_back(context);
  }
  syncTrayIcon();
}

void IbusHost::engineDisabled(IbusInputContext *context) {
  const auto before = enabledContexts_.size();
  enabledContexts_.erase(
      std::remove(enabledContexts_.begin(), enabledContexts_.end(), context),
      enabledContexts_.end());
  if (before != enabledContexts_.size()) {
    syncTrayIcon();
  }
}

void IbusHost::syncTrayIcon() {
  if (!trayItem_) {
    return;
  }
  std::string icon = engine_->overrideIcon();
  // Không phải engine đang chọn thì hiện bản xám nếu có, không thì giữ icon.
  if (!arecaActive() && iconExists(icon + "-inactive")) {
    icon += "-inactive";
  }
  trayItem_->setIconName(icon);
  trayItem_->notifyMenuChanged();
  trayItem_->setEnabled(engine_->trayIconEnabled());
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
    if (trayItem_) {
      trayItem_->notifyMenuChanged();
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
  engineDisabled(context);
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
        syncTrayIcon();
        return false;
      });
}

} // namespace areca
