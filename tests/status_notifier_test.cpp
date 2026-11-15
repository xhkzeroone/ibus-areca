#include <cassert>
#include <string>
#include <vector>

#include "ibus/status_notifier.h"

namespace {

using areca::StatusNotifierItem;
using Item = StatusNotifierItem::MenuItem;

Item makeItem(const std::string &key, const std::string &label) {
  Item item;
  item.key = key;
  item.label = label;
  return item;
}

// Đọc nhãn của một node từ GVariant (ia{sv}av) mà GetLayout trả về.
std::string labelOf(GVariant *node) {
  GVariant *props = g_variant_get_child_value(node, 1);
  GVariant *label = g_variant_lookup_value(props, "label", nullptr);
  std::string result;
  if (label) {
    result = g_variant_get_string(label, nullptr);
    g_variant_unref(label);
  }
  g_variant_unref(props);
  return result;
}

int idOf(GVariant *node) {
  gint id = -1;
  GVariant *value = g_variant_get_child_value(node, 0);
  id = g_variant_get_int32(value);
  g_variant_unref(value);
  return id;
}

// Layout gốc: (u (ia{sv}av)); trả danh sách (id, nhãn) của các con trực tiếp.
std::vector<std::pair<int, std::string>> topLevel(StatusNotifierItem &item) {
  GVariant *reply = g_variant_ref_sink(item.menuLayout(0, -1));
  GVariant *root = g_variant_get_child_value(reply, 1);
  GVariant *children = g_variant_get_child_value(root, 2);
  std::vector<std::pair<int, std::string>> result;
  for (gsize i = 0; i < g_variant_n_children(children); ++i) {
    GVariant *boxed = g_variant_get_child_value(children, i);
    GVariant *node = g_variant_get_variant(boxed);
    result.emplace_back(idOf(node), labelOf(node));
    g_variant_unref(node);
    g_variant_unref(boxed);
  }
  g_variant_unref(children);
  g_variant_unref(root);
  g_variant_unref(reply);
  return result;
}

} // namespace

int main() {
  bool inactive = false;
  int restartClicks = 0;
  StatusNotifierItem item([]() {});
  item.setMenuProvider([&]() {
    std::vector<Item> items;
    if (inactive) {
      items.push_back(makeItem("use-areca", "Sử dụng Areca"));
    } else {
      Item sub = makeItem("action:input-method", "Kiểu gõ: Telex 2");
      sub.children.push_back(makeItem("action:im-0", "Telex 2"));
      items.push_back(std::move(sub));
    }
    Item restart = makeItem("restart-ibus", "Khởi động lại IBus");
    restart.activate = [&]() { ++restartClicks; };
    items.push_back(std::move(restart));
    return items;
  });

  const auto active = topLevel(item);
  assert(active.size() == 2);
  assert(active[0].second == "Kiểu gõ: Telex 2");
  const int restartId = active[1].first;

  // Đổi trạng thái: id của mục vẫn là của chính nó, id cũ không bị dùng lại cho
  // mục khác loại, vì client giữ widget theo id.
  inactive = true;
  assert(item.menuAboutToShow(0));
  const auto idle = topLevel(item);
  assert(idle.size() == 2);
  assert(idle[1].first == restartId);
  assert(idle[0].first != active[0].first);
  assert(idle[0].second == "Sử dụng Areca");

  // Mở submenu không dựng lại menu.
  assert(!item.menuAboutToShow(idle[0].first));

  // Click đúng mục kể cả sau khi menu đổi.
  item.menuEvent(restartId, "opened");
  assert(restartClicks == 0);
  item.menuEvent(restartId, "clicked");
  assert(restartClicks == 1);

  // Id lạ không làm gì và không văng lỗi.
  item.menuEvent(9999, "clicked");
  assert(restartClicks == 1);

  // Chưa bật thì thông báo đổi menu là vô hại.
  item.notifyMenuChanged();

  // Cây con chỉ trả về theo parentId và độ sâu.
  inactive = false;
  item.menuAboutToShow(0);
  const auto again = topLevel(item);
  GVariant *sub = g_variant_ref_sink(item.menuLayout(again[0].first, 0));
  GVariant *node = g_variant_get_child_value(sub, 1);
  GVariant *kids = g_variant_get_child_value(node, 2);
  assert(g_variant_n_children(kids) == 0);
  g_variant_unref(kids);
  g_variant_unref(node);
  g_variant_unref(sub);
  return 0;
}
