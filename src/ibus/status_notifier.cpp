#include "ibus/status_notifier.h"

#include <unistd.h>

namespace areca {
namespace {

constexpr const char *kItemPath = "/StatusNotifierItem";
constexpr const char *kItemInterface = "org.kde.StatusNotifierItem";

constexpr const char *kIntrospectionXml = R"xml(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="i" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionMovieName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewAttentionIcon"/>
    <signal name="NewOverlayIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus"><arg type="s"/></signal>
  </interface>
</node>)xml";

constexpr const char *kMenuPath = "/MenuBar";
constexpr const char *kMenuInterface = "com.canonical.dbusmenu";

constexpr const char *kMenuIntrospectionXml = R"xml(
<node>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg type="i" name="parentId" direction="in"/>
      <arg type="i" name="recursionDepth" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="u" name="revision" direction="out"/>
      <arg type="(ia{sv}av)" name="layout" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="as" name="propertyNames" direction="in"/>
      <arg type="a(ia{sv})" name="properties" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="name" direction="in"/>
      <arg type="v" name="value" direction="out"/>
    </method>
    <method name="Event">
      <arg type="i" name="id" direction="in"/>
      <arg type="s" name="eventId" direction="in"/>
      <arg type="v" name="data" direction="in"/>
      <arg type="u" name="timestamp" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg type="a(isvu)" name="events" direction="in"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg type="i" name="id" direction="in"/>
      <arg type="b" name="needUpdate" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg type="ai" name="ids" direction="in"/>
      <arg type="ai" name="updatesNeeded" direction="out"/>
      <arg type="ai" name="idErrors" direction="out"/>
    </method>
    <signal name="ItemsPropertiesUpdated">
      <arg type="a(ia{sv})" name="updatedProps"/>
      <arg type="a(ias)" name="removedProps"/>
    </signal>
    <signal name="LayoutUpdated">
      <arg type="u" name="revision"/>
      <arg type="i" name="parent"/>
    </signal>
    <signal name="ItemActivationRequested">
      <arg type="i" name="id"/>
      <arg type="u" name="timestamp"/>
    </signal>
  </interface>
</node>)xml";

GDBusNodeInfo *introspection() {
  static GDBusNodeInfo *info =
      g_dbus_node_info_new_for_xml(kIntrospectionXml, nullptr);
  return info;
}

GDBusNodeInfo *menuIntrospection() {
  static GDBusNodeInfo *info =
      g_dbus_node_info_new_for_xml(kMenuIntrospectionXml, nullptr);
  return info;
}

void handleMenuMethodCall(GDBusConnection *, const gchar *, const gchar *,
                          const gchar *, const gchar *methodName,
                          GVariant *parameters,
                          GDBusMethodInvocation *invocation,
                          gpointer userData) {
  auto *item = static_cast<StatusNotifierItem *>(userData);
  const std::string method = methodName ? methodName : "";
  if (method == "GetLayout") {
    gint parentId = 0;
    gint depth = -1;
    g_variant_get(parameters, "(ii@as)", &parentId, &depth, nullptr);
    g_dbus_method_invocation_return_value(invocation,
                                          item->menuLayout(parentId, depth));
  } else if (method == "GetGroupProperties") {
    GVariant *ids = g_variant_get_child_value(parameters, 0);
    g_dbus_method_invocation_return_value(invocation,
                                          item->menuGroupProperties(ids));
    g_variant_unref(ids);
  } else if (method == "GetProperty") {
    gint id = 0;
    const gchar *name = nullptr;
    g_variant_get(parameters, "(i&s)", &id, &name);
    GVariant *value = item->menuProperty(id, name ? name : "");
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(v)", value ? value : g_variant_new_string("")));
  } else if (method == "Event") {
    gint id = 0;
    const gchar *eventId = nullptr;
    g_variant_get(parameters, "(i&svu)", &id, &eventId, nullptr, nullptr);
    item->menuEvent(id, eventId ? eventId : "");
    g_dbus_method_invocation_return_value(invocation, nullptr);
  } else if (method == "EventGroup") {
    GVariant *events = g_variant_get_child_value(parameters, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, events);
    gint id = 0;
    const gchar *eventId = nullptr;
    while (g_variant_iter_next(&iter, "(i&svu)", &id, &eventId, nullptr,
                               nullptr)) {
      item->menuEvent(id, eventId ? eventId : "");
    }
    g_variant_unref(events);
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(@ai)",
                      g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
  } else if (method == "AboutToShow") {
    gint id = 0;
    g_variant_get(parameters, "(i)", &id);
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(b)", item->menuAboutToShow(id)));
  } else if (method == "AboutToShowGroup") {
    GVariant *ids = g_variant_get_child_value(parameters, 0);
    GVariantBuilder updates;
    g_variant_builder_init(&updates, G_VARIANT_TYPE("ai"));
    GVariantIter iter;
    g_variant_iter_init(&iter, ids);
    gint id = 0;
    while (g_variant_iter_next(&iter, "i", &id)) {
      if (item->menuAboutToShow(id)) {
        g_variant_builder_add(&updates, "i", id);
      }
    }
    g_variant_unref(ids);
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new("(@ai@ai)", g_variant_builder_end(&updates),
                      g_variant_new_array(G_VARIANT_TYPE_INT32, nullptr, 0)));
  } else {
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                          G_DBUS_ERROR_UNKNOWN_METHOD,
                                          "Unknown method");
  }
}

GVariant *handleMenuGetProperty(GDBusConnection *, const gchar *, const gchar *,
                                const gchar *, const gchar *propertyName,
                                GError **, gpointer) {
  const std::string key = propertyName ? propertyName : "";
  if (key == "Version")
    return g_variant_new_uint32(3);
  if (key == "TextDirection")
    return g_variant_new_string("ltr");
  if (key == "Status")
    return g_variant_new_string("normal");
  return g_variant_new_array(G_VARIANT_TYPE_STRING, nullptr, 0);
}

GVariant *emptyPixmaps() {
  return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0);
}

void handleMethodCall(GDBusConnection *, const gchar *, const gchar *,
                      const gchar *, const gchar *methodName, GVariant *,
                      GDBusMethodInvocation *invocation, gpointer userData) {
  auto *item = static_cast<StatusNotifierItem *>(userData);
  // Item không có menu: bấm trái hay phải đều mở app cài đặt.
  if (g_strcmp0(methodName, "Activate") == 0 ||
      g_strcmp0(methodName, "ContextMenu") == 0) {
    item->activate();
  }
  g_dbus_method_invocation_return_value(invocation, nullptr);
}

GVariant *handleGetProperty(GDBusConnection *, const gchar *, const gchar *,
                            const gchar *, const gchar *propertyName, GError **,
                            gpointer userData) {
  return static_cast<StatusNotifierItem *>(userData)->property(propertyName);
}

} // namespace

StatusNotifierItem::StatusNotifierItem(ActivateHandler onActivate)
    : onActivate_(std::move(onActivate)) {}

StatusNotifierItem::~StatusNotifierItem() { stop(); }

GVariant *StatusNotifierItem::property(const char *name) const {
  const std::string key = name ? name : "";
  if (key == "Category")
    return g_variant_new_string("ApplicationStatus");
  if (key == "Id")
    return g_variant_new_string("ibus-areca");
  if (key == "Title")
    return g_variant_new_string("Areca");
  if (key == "Status")
    return g_variant_new_string(passive_ ? "Passive" : "Active");
  if (key == "WindowId")
    return g_variant_new_int32(0);
  if (key == "IconName")
    return g_variant_new_string(iconName_.c_str());
  if (key == "IconPixmap" || key == "OverlayIconPixmap" ||
      key == "AttentionIconPixmap")
    return emptyPixmaps();
  if (key == "ToolTip")
    return g_variant_new("(s@a(iiay)ss)", "", emptyPixmaps(), "Areca", "");
  if (key == "ItemIsMenu")
    return g_variant_new_boolean(FALSE);
  if (key == "Menu")
    return g_variant_new_object_path(menuProvider_ ? kMenuPath
                                                   : "/NO_DBUSMENU");
  // Icon tuỳ chọn được app cài đặt chép phẳng vào thư mục icon của người dùng.
  // Khai báo thư mục này để host tìm được tên mới mà không phụ thuộc cache
  // theme của gnome-shell, vốn chỉ nhận tên icon mới sau khi làm mới.
  if (key == "IconThemePath") {
    gchar *path = g_build_filename(g_get_user_data_dir(), "icons", nullptr);
    GVariant *value = g_variant_new_string(path);
    g_free(path);
    return value;
  }
  // OverlayIconName, AttentionIconName, AttentionMovieName.
  return g_variant_new_string("");
}

void StatusNotifierItem::setEnabled(bool enabled) {
  if (enabled == enabled_) {
    return;
  }
  enabled_ = enabled;
  if (enabled_) {
    start();
  } else {
    stop();
  }
}

void StatusNotifierItem::setIconName(const std::string &iconName) {
  const std::string next = iconName.empty() ? "ibus-areca" : iconName;
  if (next == iconName_) {
    return;
  }
  iconName_ = next;
  if (bus_ && objectId_ != 0) {
    g_dbus_connection_emit_signal(bus_, nullptr, kItemPath, kItemInterface,
                                  "NewIcon", nullptr, nullptr);
  }
}

void StatusNotifierItem::start() {
  if (bus_ || !introspection()) {
    return;
  }
  bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  if (!bus_) {
    return;
  }
  passive_ = false;
  static const GDBusInterfaceVTable vtable = {
      handleMethodCall, handleGetProperty, nullptr, {}};
  objectId_ = g_dbus_connection_register_object(
      bus_, kItemPath, introspection()->interfaces[0], &vtable, this, nullptr,
      nullptr);
  if (objectId_ == 0) {
    stop();
    return;
  }
  if (menuProvider_ && menuIntrospection()) {
    static const GDBusInterfaceVTable menuVtable = {
        handleMenuMethodCall, handleMenuGetProperty, nullptr, {}};
    menuObjectId_ = g_dbus_connection_register_object(
        bus_, kMenuPath, menuIntrospection()->interfaces[0], &menuVtable, this,
        nullptr, nullptr);
  }

  // Mỗi lần bật dùng một tên mới. Extension AppIndicator có thể còn giữ mục cũ
  // (đã Passive) theo tên bus và bỏ qua lần đăng ký trùng tên, nên icon không
  // hiện lại.
  busName_ = "org.kde.StatusNotifierItem-" + std::to_string(getpid()) + "-" +
             std::to_string(++startCount_);
  ownerId_ = g_bus_own_name_on_connection(
      bus_, busName_.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE,
      +[](GDBusConnection *, const gchar *, gpointer data) {
        auto *self = static_cast<StatusNotifierItem *>(data);
        self->nameAcquired_ = true;
        self->registerWithWatcher();
      },
      nullptr, this, nullptr);
  // Watcher có thể chưa chạy lúc đăng nhập (extension nạp sau), nên đăng ký lại
  // mỗi khi nó xuất hiện.
  watcherId_ = g_bus_watch_name_on_connection(
      bus_, "org.kde.StatusNotifierWatcher", G_BUS_NAME_WATCHER_FLAGS_NONE,
      +[](GDBusConnection *, const gchar *, const gchar *, gpointer data) {
        static_cast<StatusNotifierItem *>(data)->registerWithWatcher();
      },
      nullptr, this, nullptr);
}

void StatusNotifierItem::registerWithWatcher() {
  if (!bus_ || !nameAcquired_) {
    return;
  }
  g_dbus_connection_call(
      bus_, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
      "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem",
      g_variant_new("(s)", busName_.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE,
      -1, nullptr, nullptr, nullptr);
}

void StatusNotifierItem::stop() {
  if (watcherId_ != 0) {
    g_bus_unwatch_name(watcherId_);
    watcherId_ = 0;
  }
  if (ownerId_ != 0) {
    g_bus_unown_name(ownerId_);
    ownerId_ = 0;
  }
  nameAcquired_ = false;
  if (bus_ && objectId_ != 0) {
    // Extension AppIndicator có thể không dọn kịp khi tên biến mất, nên báo
    // Passive trước để icon bị ẩn ngay. Tin nhắn đi cùng một kết nối theo đúng
    // thứ tự nên không cần chờ flush.
    passive_ = true;
    g_dbus_connection_emit_signal(bus_, nullptr, kItemPath, kItemInterface,
                                  "NewStatus", g_variant_new("(s)", "Passive"),
                                  nullptr);
    g_dbus_connection_unregister_object(bus_, objectId_);
  }
  if (bus_ && menuObjectId_ != 0) {
    g_dbus_connection_unregister_object(bus_, menuObjectId_);
  }
  objectId_ = 0;
  menuObjectId_ = 0;
  menuNodes_.clear();
  menuDirty_ = true;
  if (bus_) {
    g_object_unref(bus_);
    bus_ = nullptr;
  }
}

int StatusNotifierItem::addMenuNode(const MenuItem &item, int parent,
                                    int index) {
  const std::string key = item.key.empty()
                              ? "p" + std::to_string(parent) + "#" +
                                    std::to_string(index) + ":" + item.label
                              : item.key;
  auto found = menuIds_.find(key);
  if (found == menuIds_.end()) {
    found = menuIds_.emplace(key, ++lastMenuId_).first;
  }
  const int id = found->second;
  MenuNode node;
  node.id = id;
  node.parent = parent;
  node.item = item;
  node.item.children.clear();
  menuNodes_[id] = std::move(node);
  std::vector<int> childIds;
  int childIndex = 0;
  for (const auto &child : item.children) {
    childIds.push_back(addMenuNode(child, id, childIndex++));
  }
  menuNodes_[id].children = std::move(childIds);
  return id;
}

void StatusNotifierItem::rebuildMenu() {
  menuNodes_.clear();
  MenuNode root;
  root.id = 0;
  menuNodes_[0] = std::move(root);
  std::vector<int> top;
  if (menuProvider_) {
    int index = 0;
    for (const auto &item : menuProvider_()) {
      top.push_back(addMenuNode(item, 0, index++));
    }
  }
  menuNodes_[0].children = std::move(top);
  menuDirty_ = false;
}

GVariant *StatusNotifierItem::menuNodeProperties(const MenuNode &node) const {
  GVariantBuilder props;
  g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
  if (node.id == 0) {
    g_variant_builder_add(&props, "{sv}", "children-display",
                          g_variant_new_string("submenu"));
    return g_variant_builder_end(&props);
  }
  const auto &item = node.item;
  if (item.separator) {
    g_variant_builder_add(&props, "{sv}", "type",
                          g_variant_new_string("separator"));
    return g_variant_builder_end(&props);
  }
  g_variant_builder_add(&props, "{sv}", "label",
                        g_variant_new_string(item.label.c_str()));
  g_variant_builder_add(&props, "{sv}", "enabled",
                        g_variant_new_boolean(item.enabled));
  g_variant_builder_add(&props, "{sv}", "visible", g_variant_new_boolean(TRUE));
  if (item.toggle != MenuItem::Toggle::None) {
    g_variant_builder_add(
        &props, "{sv}", "toggle-type",
        g_variant_new_string(
            item.toggle == MenuItem::Toggle::Radio ? "radio" : "checkmark"));
    g_variant_builder_add(&props, "{sv}", "toggle-state",
                          g_variant_new_int32(item.checked ? 1 : 0));
  }
  if (!node.children.empty()) {
    g_variant_builder_add(&props, "{sv}", "children-display",
                          g_variant_new_string("submenu"));
  }
  return g_variant_builder_end(&props);
}

GVariant *StatusNotifierItem::menuNodeLayout(int id, int depth) const {
  const auto it = menuNodes_.find(id);
  GVariantBuilder children;
  g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
  if (it == menuNodes_.end()) {
    return g_variant_new("(i@a{sv}@av)", id, g_variant_builder_end(&children),
                         nullptr);
  }
  // depth: -1 lấy hết cây con, 0 chỉ lấy chính node này.
  if (depth != 0) {
    for (int childId : it->second.children) {
      g_variant_builder_add(
          &children, "v", menuNodeLayout(childId, depth < 0 ? -1 : depth - 1));
    }
  }
  return g_variant_new("(i@a{sv}@av)", id, menuNodeProperties(it->second),
                       g_variant_builder_end(&children));
}

GVariant *StatusNotifierItem::menuLayout(int parentId, int depth) {
  if (menuDirty_ || menuNodes_.empty()) {
    rebuildMenu();
  }
  if (menuNodes_.find(parentId) == menuNodes_.end()) {
    parentId = 0;
  }
  return g_variant_new("(u@(ia{sv}av))", menuRevision_,
                       menuNodeLayout(parentId, depth));
}

GVariant *StatusNotifierItem::menuGroupProperties(GVariant *ids) {
  if (menuDirty_ || menuNodes_.empty()) {
    rebuildMenu();
  }
  GVariantBuilder result;
  g_variant_builder_init(&result, G_VARIANT_TYPE("a(ia{sv})"));
  GVariantIter iter;
  g_variant_iter_init(&iter, ids);
  gint id = 0;
  while (g_variant_iter_next(&iter, "i", &id)) {
    const auto it = menuNodes_.find(id);
    if (it != menuNodes_.end()) {
      g_variant_builder_add(&result, "(i@a{sv})", id,
                            menuNodeProperties(it->second));
    }
  }
  return g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&result));
}

GVariant *StatusNotifierItem::menuProperty(int id, const std::string &name) {
  const auto it = menuNodes_.find(id);
  if (it == menuNodes_.end()) {
    return nullptr;
  }
  GVariant *props = menuNodeProperties(it->second);
  GVariant *value = g_variant_lookup_value(props, name.c_str(), nullptr);
  g_variant_unref(g_variant_ref_sink(props));
  return value;
}

void StatusNotifierItem::menuEvent(int id, const std::string &eventId) {
  if (eventId != "clicked") {
    return;
  }
  const auto it = menuNodes_.find(id);
  if (it == menuNodes_.end() || !it->second.item.activate) {
    return;
  }
  // Menu có thể dựng lại trong lúc xử lý, nên chép hàm ra trước.
  auto callback = it->second.item.activate;
  menuDirty_ = true;
  callback();
}

void StatusNotifierItem::notifyMenuChanged() {
  if (!bus_ || menuObjectId_ == 0) {
    return;
  }
  rebuildMenu();
  ++menuRevision_;
  g_dbus_connection_emit_signal(
      bus_, nullptr, kMenuPath, kMenuInterface, "LayoutUpdated",
      g_variant_new("(ui)", menuRevision_, 0), nullptr);
}

bool StatusNotifierItem::menuAboutToShow(int id) {
  // Chỉ dựng lại khi mở menu gốc. Mở submenu mà dựng lại sẽ làm client đang
  // giữ node cũ bị lệch, và id tuần tự vẫn đúng vì dựng theo cùng thứ tự.
  if (id != 0) {
    return false;
  }
  rebuildMenu();
  ++menuRevision_;
  return true;
}

} // namespace areca
