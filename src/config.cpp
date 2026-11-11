#include "config.h"

#include <glib.h>

namespace areca {
namespace {

std::string_view trim(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r";
  const auto begin = text.find_first_not_of(kSpace);
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(kSpace);
  return text.substr(begin, end - begin + 1);
}

bool parseBool(std::string_view value, bool &out) {
  if (value == "True" || value == "true") {
    out = true;
    return true;
  }
  if (value == "False" || value == "false") {
    out = false;
    return true;
  }
  return false;
}

} // namespace

Config parseConfig(std::string_view text) {
  Config config;
  while (!text.empty()) {
    const auto newline = text.find('\n');
    const auto line = trim(text.substr(0, newline));
    text = newline == std::string_view::npos ? std::string_view{}
                                             : text.substr(newline + 1);
    if (line.empty() || line.front() == '#' || line.front() == ';' ||
        line.front() == '[') {
      continue;
    }
    const auto equal = line.find('=');
    if (equal == std::string_view::npos) {
      continue;
    }
    const auto key = trim(line.substr(0, equal));
    auto value = trim(line.substr(equal + 1));
    // Bản fcitx5 ghi giá trị có dấu cách trong ngoặc kép.
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    if (key == "BambooInputMethod" && !value.empty()) {
      config.inputMethod = value;
    } else if (key == "OutputCharset" && !value.empty()) {
      config.outputCharset = value;
    } else if (key == "SpellcheckMode") {
      // Nhận cả tên enum lẫn nhãn tiếng Việt mà bản fcitx5 ghi vào file.
      if (value == "Off" || value == "Không kiểm tra (Tắt)") {
        config.spellcheckMode = SpellcheckMode::Off;
      } else if (value == "Basic" || value == "Khôi phục từ sau khi gõ xong") {
        config.spellcheckMode = SpellcheckMode::Basic;
      } else if (value == "Realtime" ||
                 value == "Khôi phục từ ngay trong lúc gõ") {
        config.spellcheckMode = SpellcheckMode::Realtime;
      }
    } else if (key == "ModernStyle") {
      parseBool(value, config.modernStyle);
    } else if (key == "Debug") {
      parseBool(value, config.debug);
    }
  }
  return config;
}

std::string configPath() {
  gchar *path = g_build_filename(g_get_user_config_dir(), "ibus-areca",
                                 "areca.conf", nullptr);
  std::string result(path);
  g_free(path);
  return result;
}

Config loadConfig() {
  gchar *contents = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(configPath().c_str(), &contents, &length, nullptr)) {
    return {};
  }
  auto config = parseConfig(std::string_view(contents, length));
  g_free(contents);
  return config;
}

} // namespace areca
