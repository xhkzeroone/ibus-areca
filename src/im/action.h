#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace areca::im {

class InputContext;
class Menu;
class SimpleAction;

// Báo lớp giao diện (IBusProperty) khi một action đổi trạng thái.
using ActionUpdateHook = std::function<void(SimpleAction *, InputContext *)>;

class ScopedConnection {
public:
  ScopedConnection() = default;
};

// Mục trên menu trạng thái của bộ gõ: có thể có menu con hoặc là ô chọn.
class SimpleAction {
public:
  struct Activated {};
  using ActivatedCallback = std::function<void(InputContext *)>;

  const std::string &name() const { return name_; }
  void setName(std::string name) { name_ = std::move(name); }
  const std::string &icon() const { return icon_; }
  void setIcon(std::string icon) { icon_ = std::move(icon); }
  const std::string &shortText() const { return shortText_; }
  void setShortText(std::string text) { shortText_ = std::move(text); }
  const std::string &longText() const { return longText_; }
  void setLongText(std::string text) { longText_ = std::move(text); }
  bool isCheckable() const { return checkable_; }
  void setCheckable(bool checkable) { checkable_ = checkable; }
  bool isChecked() const { return checked_; }
  void setChecked(bool checked) { checked_ = checked; }
  Menu *menu() const { return menu_; }
  void setMenu(Menu *menu) { menu_ = menu; }

  template <typename Signal> ScopedConnection connect(ActivatedCallback cb) {
    callbacks_.push_back(std::move(cb));
    return {};
  }
  void activate(InputContext *inputContext) {
    for (const auto &callback : callbacks_) {
      callback(inputContext);
    }
  }
  void setUpdateHook(ActionUpdateHook hook) { updateHook_ = std::move(hook); }
  void update(InputContext *inputContext) {
    if (updateHook_) {
      updateHook_(this, inputContext);
    }
  }

private:
  std::string name_;
  std::string icon_;
  std::string shortText_;
  std::string longText_;
  bool checkable_ = false;
  bool checked_ = false;
  Menu *menu_ = nullptr;
  std::vector<ActivatedCallback> callbacks_;
  ActionUpdateHook updateHook_;
};

class Menu {
public:
  void addAction(SimpleAction *action) { actions_.push_back(action); }
  const std::vector<SimpleAction *> &actions() const { return actions_; }

private:
  std::vector<SimpleAction *> actions_;
};

enum class StatusGroup { InputMethod };

// Danh sách action hiện trên menu trạng thái của một input context.
class StatusArea {
public:
  void addAction(StatusGroup, SimpleAction *action) {
    for (auto *existing : actions_) {
      if (existing == action) {
        return;
      }
    }
    actions_.push_back(action);
  }
  void clear() { actions_.clear(); }
  const std::vector<SimpleAction *> &actions() const { return actions_; }

private:
  std::vector<SimpleAction *> actions_;
};

// Đặt tên cho action và nối mọi action vào cùng một hook cập nhật.
class UserInterfaceManager {
public:
  void registerAction(const std::string &name, SimpleAction *action) {
    action->setName(name);
    action->setUpdateHook(
        [this](SimpleAction *changed, InputContext *inputContext) {
          if (hook_) {
            hook_(changed, inputContext);
          }
        });
    actions_.push_back(action);
  }
  void setUpdateHook(ActionUpdateHook hook) { hook_ = std::move(hook); }
  SimpleAction *lookupAction(const std::string &name) const {
    for (auto *action : actions_) {
      if (action->name() == name) {
        return action;
      }
    }
    return nullptr;
  }

private:
  std::vector<SimpleAction *> actions_;
  ActionUpdateHook hook_;
};

// Chạy chương trình nền, không chờ.
void startProcess(const std::vector<std::string> &args);

} // namespace areca::im
