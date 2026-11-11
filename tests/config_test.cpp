#include <cassert>
#include <iostream>

#include "config.h"

int main() {
  using areca::parseConfig;
  using areca::SpellcheckMode;

  const auto defaults = parseConfig("");
  assert(defaults.inputMethod == "Telex 2");
  assert(defaults.outputCharset == "Unicode");
  assert(defaults.spellcheckMode == SpellcheckMode::Realtime);
  assert(defaults.modernStyle);
  assert(!defaults.debug);

  const auto parsed = parseConfig("# comment\n"
                                  "[Group]\n"
                                  "BambooInputMethod=VNI\n"
                                  "OutputCharset = TCVN3 (ABC)\r\n"
                                  "SpellcheckMode=Off\n"
                                  "ModernStyle=False\n"
                                  "Debug=True");
  assert(parsed.inputMethod == "VNI");
  assert(parsed.outputCharset == "TCVN3 (ABC)");
  assert(parsed.spellcheckMode == SpellcheckMode::Off);
  assert(!parsed.modernStyle);
  assert(parsed.debug);

  // File chép từ bản fcitx5: nhãn tiếng Việt trong ngoặc kép.
  const auto fcitx = parseConfig(
      "SpellcheckMode=\"Khôi phục từ sau khi gõ xong\"\nModernStyle=maybe\n");
  assert(fcitx.spellcheckMode == SpellcheckMode::Basic);
  assert(fcitx.modernStyle);

  std::cout << "config_test passed\n";
  return 0;
}
