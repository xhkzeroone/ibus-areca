#include "ibus/ibus_emoji_loader.h"

#include <ibus.h>

namespace areca {

namespace {

std::string findIbusEmojiDictionary(const std::string &language) {
  const std::string name = "emoji-" + language + ".dict";
  for (const gchar *const *dir = g_get_system_data_dirs(); dir && *dir; ++dir) {
    gchar *path =
        g_build_filename(*dir, "ibus", "dicts", name.c_str(), nullptr);
    std::string result(path);
    g_free(path);
    if (g_file_test(result.c_str(), G_FILE_TEST_IS_REGULAR)) {
      return result;
    }
  }
  return {};
}

} // namespace

std::vector<EmojiAnnotation>
loadIbusEmojiAnnotations(const std::string &language) {
  std::vector<EmojiAnnotation> result;
  const std::string path = findIbusEmojiDictionary(language);
  if (path.empty()) {
    return result;
  }
  // File lưu tên kiểu GObject của từng mục; kiểu phải được đăng ký trước khi
  // giải tuần tự, nhất là khi nạp trên thread riêng.
  g_type_ensure(IBUS_TYPE_EMOJI_DATA);
  GSList *list = ibus_emoji_data_load(path.c_str());
  for (GSList *item = list; item; item = item->next) {
    auto *data = IBUS_EMOJI_DATA(item->data);
    const gchar *emoji = ibus_emoji_data_get_emoji(data);
    if (!emoji || !*emoji) {
      continue;
    }
    EmojiAnnotation entry;
    entry.emoji = emoji;
    for (GSList *annotation = ibus_emoji_data_get_annotations(data); annotation;
         annotation = annotation->next) {
      if (const auto *keyword = static_cast<const gchar *>(annotation->data)) {
        entry.keywords.emplace_back(keyword);
      }
    }
    // Tên đầy đủ như "mặt cười toét miệng" cũng là từ khoá người dùng hay gõ.
    if (const gchar *description = ibus_emoji_data_get_description(data);
        description && *description) {
      entry.keywords.emplace_back(description);
    }
    result.push_back(std::move(entry));
  }
  g_slist_free_full(list, g_object_unref);
  return result;
}

} // namespace areca
