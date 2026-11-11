#pragma once

#include <string>
#include <string_view>

namespace areca {

enum class SpellcheckMode { Off, Basic, Realtime };

// Cấu hình của ibus-areca, đọc từ ~/.config/ibus-areca/areca.conf. Khoá và giá
// trị giữ như areca.conf của bản fcitx5 để người dùng chép qua được.
struct Config {
  std::string inputMethod = "Telex 2";
  std::string outputCharset = "Unicode";
  SpellcheckMode spellcheckMode = SpellcheckMode::Realtime;
  bool modernStyle = true;
  bool debug = false;
};

// Dòng dạng `Key=Value`; bỏ qua dòng trống, comment `#`/`;`, tiêu đề nhóm và
// giá trị không hợp lệ để giữ mặc định.
Config parseConfig(std::string_view text);

std::string configPath();
Config loadConfig();

} // namespace areca
