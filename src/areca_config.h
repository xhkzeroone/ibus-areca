#pragma once

#include <string>
#include <utility>
#include <vector>

#include "im/config.h"
#include "im/im.h"

namespace areca {

enum class PresentationMode { Rewrite, Preedit, Redirect };
ARECA_CONFIG_ENUM_NAME(PresentationMode, N_("Rewrite trực tiếp"), N_("Preedit"),
                       N_("Redirect (EN)"));

enum class SpellcheckMode { Off, Basic, Realtime };
ARECA_CONFIG_ENUM_NAME(SpellcheckMode, N_("Không kiểm tra (Tắt)"),
                       N_("Khôi phục từ sau khi gõ xong"),
                       N_("Khôi phục từ ngay trong lúc gõ"));

enum class MacroBypassModifier { Disabled, Shift, Control, Alt, Super };
ARECA_CONFIG_ENUM_NAME(MacroBypassModifier, N_("Tắt"), N_("Shift"), N_("Ctrl"),
                       N_("Alt"), N_("Super"));

inline bool isMacroBypassSpace(const im::Key &key,
                               MacroBypassModifier modifier) {
  const auto normalized = key.normalize();
  if (normalized.sym() != im::Key_space &&
      normalized.sym() != im::Key_KP_Space) {
    return false;
  }
  const auto states = normalized.states();
  const bool shift = states.test(im::KeyState::Shift);
  const bool control = states.test(im::KeyState::Ctrl);
  const bool alt = states.test(im::KeyState::Alt);
  const bool super =
      states.test(im::KeyState::Super) || states.test(im::KeyState::Meta) ||
      states.test(im::KeyState::Hyper) || states.test(im::KeyState::Super2) ||
      states.test(im::KeyState::Hyper2);
  switch (modifier) {
  case MacroBypassModifier::Shift:
    return shift && !control && !alt && !super;
  case MacroBypassModifier::Control:
    return control && !shift && !alt && !super;
  case MacroBypassModifier::Alt:
    return alt && !shift && !control && !super;
  case MacroBypassModifier::Super:
    return super && !shift && !control && !alt;
  case MacroBypassModifier::Disabled:
    return false;
  }
  return false;
}

inline bool shouldForceBackspaceForUrl(bool enabled,
                                       const im::CapabilityFlags &capabilities,
                                       bool inAddressBar = false) {
  return enabled &&
         (capabilities.test(im::CapabilityFlag::Url) || inAddressBar);
}

inline bool shouldUseShiftSelectForUrl(bool isUrl, bool hasSurrounding,
                                       bool isBrowser) {
  return isUrl && !hasSurrounding && isBrowser;
}

struct StringListAnnotation : public im::EnumAnnotation {
  void setList(std::vector<std::string> list) { list_ = std::move(list); }

  void dumpDescription(im::RawConfig &config) const {
    im::EnumAnnotation::dumpDescription(config);
    for (size_t i = 0; i < list_.size(); ++i) {
      config.setValueByPath("Enum/" + std::to_string(i), list_[i]);
    }
  }

private:
  std::vector<std::string> list_;
};

enum class AppBackendMode {
  Auto = 0,
  SurroundingText = 1,
  ShiftSelect = 2,
  Backspace = 3,
  UinputShiftSelect = 2,
  NativeShiftSelect = 2,
  NativeXTest = 3,
  UinputBackspace = 3,
  ForwardKey = 3,
  Preedit = 4,
  Redirect = 5
};
ARECA_CONFIG_ENUM_NAME(AppBackendMode, N_("Tự động"), N_("Surrounding"),
                       N_("Shift select"), N_("Backspace"), N_("Preedit"),
                       N_("Redirect (EN)"));

inline PresentationMode
presentationModeForAppSetting(AppBackendMode mode, PresentationMode fallback) {
  switch (mode) {
  case AppBackendMode::Preedit:
    return PresentationMode::Preedit;
  case AppBackendMode::Redirect:
    return PresentationMode::Redirect;
  case AppBackendMode::SurroundingText:
  case AppBackendMode::ShiftSelect:
  case AppBackendMode::Backspace:
    return PresentationMode::Rewrite;
  case AppBackendMode::Auto:
    return fallback;
  }
  return fallback;
}

ARECA_CONFIGURATION(
    AppBackendOverrideEntry,
    im::Option<std::string> appName{this, "AppName", N_("Tên ứng dụng"), ""};
    im::OptionWithAnnotation<AppBackendMode, im::EnumAnnotation> mode{
        this, "Mode", N_("Chế độ / backend"), AppBackendMode::Auto};);

ARECA_CONFIGURATION(
    MacroEntry, im::Option<std::string> key{this, "Key", N_("Từ viết tắt"), ""};
    im::Option<std::string> value{this, "Value", N_("Nội dung thay thế"), ""};);

ARECA_CONFIGURATION(
    AppProfilesConfig,
    im::OptionWithAnnotation<std::vector<AppBackendOverrideEntry>,
                             im::ListDisplayOptionAnnotation>
        appOverrides{this,
                     "AppOverrides",
                     N_("Cấu hình chế độ / backend theo ứng dụng"),
                     {},
                     {},
                     {},
                     im::ListDisplayOptionAnnotation("AppName")};);

ARECA_CONFIGURATION(MacroTableConfig,
                    im::OptionWithAnnotation<std::vector<MacroEntry>,
                                             im::ListDisplayOptionAnnotation>
                        macros{this,
                               "Macro",
                               N_("Danh sách macro"),
                               {},
                               {},
                               {},
                               im::ListDisplayOptionAnnotation("Key")};);

ARECA_CONFIGURATION(
    AdvancedConfig,
    im::Option<int, im::IntConstrain> backspaceDelayMs{
        this, "BackspaceDelayMs", N_("Delay giữa các Backspace (ms)"), 1,
        im::IntConstrain(0, 1000)};
    im::HiddenOption<int, im::IntConstrain> uinputBackspaceAckTimeoutMs{
        this, "UinputBackspaceAckTimeoutMs",
        N_("Timeout ACK uinput Backspace (ms)"), 150,
        im::IntConstrain(1, 5000)};
    im::Option<int, im::IntConstrain> afterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs", N_("Chờ sau Backspace cuối (ms)"), 10,
        im::IntConstrain(0, 5000)};
    im::Option<int, im::IntConstrain> uinputShiftSelectDelayMs{
        this, "UinputShiftSelectDelayMs",
        N_("Delay giữa các phím uinput Shift+Left (ms)"), 1,
        im::IntConstrain(0, 1000)};
    im::HiddenOption<int, im::IntConstrain> uinputShiftSelectAckTimeoutMs{
        this, "UinputShiftSelectAckTimeoutMs",
        N_("Timeout ACK uinput Shift+Left (ms)"), 150,
        im::IntConstrain(1, 5000)};
    im::Option<int, im::IntConstrain> afterUinputShiftSelectWaitMs{
        this, "AfterUinputShiftSelectWaitMs",
        N_("Chờ sau khi thả Shift uinput (ms)"), 10, im::IntConstrain(0, 5000)};
    im::Option<int, im::IntConstrain> surroundingWaitMs{
        this, "SurroundingWaitMs", N_("Chờ sau xóa surrounding text (ms)"), 3,
        im::IntConstrain(0, 5000)};
    im::HiddenOption<int, im::IntConstrain> surroundingProbeTimeoutMs{
        this, "SurroundingProbeTimeoutMs",
        N_("Timeout kiểm tra surrounding text (ms)"), 80,
        im::IntConstrain(1, 1000)};
    im::Option<int, im::IntConstrain> surroundingDeleteDelayMs{
        this, "SurroundingDeleteDelayMs",
        N_("Delay giữa các lệnh xóa surrounding v2 (ms)"), 10,
        im::IntConstrain(0, 1000)};
    im::Option<int, im::IntConstrain> afterSurroundingDeleteWaitMs{
        this, "AfterSurroundingDeleteWaitMs",
        N_("Chờ sau lệnh xóa surrounding v2 cuối (ms)"), 1,
        im::IntConstrain(0, 5000)};
    im::Option<int, im::IntConstrain> xtestBackspaceDelayMs{
        this, "XTestBackspaceDelayMs",
        N_("Delay giữa các Backspace Native (ms)"), 1,
        im::IntConstrain(0, 1000)};
    im::Option<int, im::IntConstrain> afterXTestBackspaceWaitMs{
        this, "AfterXTestBackspaceWaitMs",
        N_("Chờ sau Backspace cuối Native (ms)"), 10,
        im::IntConstrain(0, 5000)};
    im::Option<int, im::IntConstrain> postCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        im::IntConstrain(0, 5000)};
    im::Option<bool> preciseTiming{this, "PreciseTiming",
                                   N_("Dùng timer độ chính xác cao"), true};
    im::Option<bool> useSurroundingV2InsteadOfSurroundingV1{
        this, "UseSurroundingV2InsteadOfSurroundingV1",
        N_("Dùng Surrounding v2 thay cho Surrounding v1"), false};
    im::Option<bool> useUinputInsteadOfForwardKey{
        this, "UseUinputInsteadOfForwardKey",
        N_("Dùng uinput thay cho ForwardKey"), true};
    im::Option<bool> useXTestInsteadOfUinput{
        this, "UseXTestInsteadOfUinput",
        N_("Dùng Native (Libei) thay thế uinput"), false};
    im::Option<bool> enableMouseTracking{
        this, "EnableMouseTracking", N_("Tự reset bộ gõ sau khi click chuột"),
        true};
    im::Option<bool> forwardFirstCharacter{
        this, "ForwardFirstCharacter",
        N_("Chuyển tiếp phím đầu để tương thích trình duyệt"), true};);

ARECA_CONFIGURATION(
    ArecaConfig,
    im::HiddenOption<PresentationMode> presentationMode{
        this, "PresentationMode", N_("Chế độ hiển thị"),
        PresentationMode::Rewrite};
    im::HiddenOption<im::KeyList, im::ListConstrain<im::KeyConstrain>>
        switchModeKey{
            this,
            "SwitchModeKey",
            N_("Phím tắt chuyển chế độ gõ"),
            {},
            im::KeyListConstrain(im::KeyConstrainFlag::AllowModifierLess)};
    im::KeyListOption selectBackendKey{
        this,
        "SelectBackendKey",
        N_("Phím tắt chọn chế độ / backend cho ứng dụng"),
        {im::Key("grave")},
        im::KeyListConstrain(im::KeyConstrainFlag::AllowModifierLess)};
    im::HiddenOption<int, im::IntConstrain> legacyBackspaceDelayMs{
        this, "BackspaceDelayMs", N_("Delay giữa các Backspace (ms)"), 1,
        im::IntConstrain(0, 1000)};
    im::HiddenOption<int, im::IntConstrain> legacyAfterBackspaceWaitMs{
        this, "AfterBackspaceWaitMs",
        N_("Thời gian chờ sau Backspace cuối (ms)"), 10,
        im::IntConstrain(0, 5000)};
    im::HiddenOption<int, im::IntConstrain> legacyPostCommitDelayMs{
        this, "PostCommitDelayMs", N_("Delay sau mỗi commit (ms)"), 20,
        im::IntConstrain(0, 5000)};
    im::HiddenOption<std::string> trayIcon{
        this, "TrayIcon", N_("Biểu tượng khay hệ thống"), "ibus-areca"};
    im::OptionWithAnnotation<std::string, StringListAnnotation>
        bambooInputMethod{this, "BambooInputMethod", N_("Kiểu gõ Bamboo"),
                          "Telex 2"};
    im::OptionWithAnnotation<std::string, StringListAnnotation> outputCharset{
        this, "OutputCharset", N_("Bảng mã đầu ra"), "Unicode"};
    im::OptionWithAnnotation<SpellcheckMode, im::EnumAnnotation> spellcheckMode{
        this, "SpellcheckMode", N_("Chế độ kiểm tra chính tả"),
        SpellcheckMode::Realtime};
    im::Option<bool> modernStyle{
        this, "ModernStyle", N_("Đặt dấu kiểu oà, uý thay cho òa, úy"), true};
    im::Option<bool> autoCapitalizeAfterPunctuation{
        this, "AutoCapitalizeAfterPunctuation",
        N_("Tự viết hoa sau dấu kết câu (. ! ?)"), false};
    im::Option<bool> enableMacro{this, "EnableMacro", N_("Bật macro"), true};
    im::Option<bool> capitalizeMacro{this, "CapitalizeMacro",
                                     N_("Tự đổi hoa/thường cho nội dung macro"),
                                     true};
    im::Option<bool> revertMacroOnBackspace{
        this, "RevertMacroOnBackspace", N_("Hoàn tác macro khi nhấn Backspace"),
        true};
    im::OptionWithAnnotation<MacroBypassModifier, im::EnumAnnotation>
        macroBypassModifier{this, "MacroBypassModifier",
                            N_("Phím giữ để tạm bỏ qua macro khi nhấn Space"),
                            MacroBypassModifier::Shift};
    im::Option<bool> backspaceRecovery{
        this, "BackspaceRecovery",
        N_("Khôi phục lỗi chính tả khi nhấn Backspace"), true};
    im::Option<bool> restoreSurroundingText{
        this, "RestoreSurroundingText", N_("Cho phép sửa từ đã commit"), false};
    im::Option<bool> shiftSelectFallbackForBrowser{
        this, "ShiftSelectFallbackForBrowser",
        N_("Dùng Shift+Left làm fallback khi trình duyệt không hỗ trợ "
           "surrounding text"),
        false};
    im::Option<bool> useBackspaceForUrl{
        this, "UseBackspaceForUrl", N_("Xử lý riêng ô URL và thanh địa chỉ"),
        true};
    im::Option<bool> disableVietnameseInTerminal{
        this, "DisableVietnameseInTerminal",
        N_("Tắt tiếng Việt trong terminal"), false};
    im::Option<bool> debug{this, "Debug", N_("Bật log debug Areca"), false};);

} // namespace areca
