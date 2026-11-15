#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gio/gio.h>

namespace areca {

// Biểu tượng khay theo giao thức StatusNotifierItem. GNOME chỉ vẽ chữ cho
// chỉ báo Input Source nên muốn hiện ảnh phải tự đăng ký một item riêng, và
// cần extension AppIndicator để thấy nó. Mọi hàm chạy trên GLib main loop.
class StatusNotifierItem final {
public:
  using ActivateHandler = std::function<void()>;

  // Mục trên menu chuột phải (giao thức com.canonical.dbusmenu).
  struct MenuItem {
    enum class Toggle { None, Check, Radio };

    // Khoá cố định của mục. Client giữ widget theo id, nên một id không được
    // đổi sang mục khác loại; id lấy từ khoá để ổn định qua các lần dựng lại.
    std::string key;
    std::string label;
    bool separator = false;
    bool enabled = true;
    Toggle toggle = Toggle::None;
    bool checked = false;
    std::function<void()> activate;
    std::vector<MenuItem> children;
  };
  // Dựng lại menu mỗi lần mở để trạng thái luôn mới.
  using MenuProvider = std::function<std::vector<MenuItem>()>;

  explicit StatusNotifierItem(ActivateHandler onActivate);
  ~StatusNotifierItem();

  StatusNotifierItem(const StatusNotifierItem &) = delete;
  StatusNotifierItem &operator=(const StatusNotifierItem &) = delete;

  void setEnabled(bool enabled);
  void setIconName(const std::string &iconName);
  void setMenuProvider(MenuProvider provider) {
    menuProvider_ = std::move(provider);
  }
  // Nội dung menu vừa đổi: dựng lại và báo client lấy lại layout.
  void notifyMenuChanged();

  GVariant *property(const char *name) const;
  // Phía dbusmenu, do handler D-Bus gọi.
  GVariant *menuLayout(int parentId, int depth);
  GVariant *menuGroupProperties(GVariant *ids);
  GVariant *menuProperty(int id, const std::string &name);
  void menuEvent(int id, const std::string &eventId);
  bool menuAboutToShow(int id);
  void activate() const {
    if (onActivate_)
      onActivate_();
  }

private:
  void start();
  void stop();
  void registerWithWatcher();
  struct MenuNode {
    int id = 0;
    int parent = 0;
    MenuItem item;
    std::vector<int> children;
  };
  void rebuildMenu();
  int addMenuNode(const MenuItem &item, int parent, int index);
  GVariant *menuNodeProperties(const MenuNode &node) const;
  GVariant *menuNodeLayout(int id, int depth) const;

  ActivateHandler onActivate_;
  std::string iconName_ = "ibus-areca";
  std::string busName_;
  GDBusConnection *bus_ = nullptr;
  guint objectId_ = 0;
  guint menuObjectId_ = 0;
  MenuProvider menuProvider_;
  std::unordered_map<int, MenuNode> menuNodes_;
  bool menuDirty_ = true;
  int lastMenuId_ = 0;
  std::unordered_map<std::string, int> menuIds_;
  guint menuRevision_ = 1;
  guint ownerId_ = 0;
  guint watcherId_ = 0;
  bool nameAcquired_ = false;
  bool enabled_ = false;
  bool passive_ = false;
  int startCount_ = 0;
};

} // namespace areca
