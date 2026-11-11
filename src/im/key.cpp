#include "key.h"

#include <string_view>

#include <xkbcommon/xkbcommon.h>

#include "utf8.h"

namespace areca::im {
namespace {

struct ModifierName {
  std::string_view name;
  KeyState state;
};

constexpr ModifierName kModifierNames[] = {
    {"Control", KeyState::Ctrl}, {"Ctrl", KeyState::Ctrl},
    {"Alt", KeyState::Alt},      {"Shift", KeyState::Shift},
    {"Super", KeyState::Super},  {"Hyper", KeyState::Hyper},
    {"Meta", KeyState::Meta},
};

KeyStates simpleStates(KeyStates states) {
  auto result = states & KeyStates(KeyState::SimpleMask);
  // Client GTK báo Super ảo ở bit riêng; coi như Super thật để so phím tắt.
  if (result.test(KeyState::Super2)) {
    result.unset(KeyState::Super2);
    result.set(KeyState::Super);
  }
  if (result.test(KeyState::Hyper2)) {
    result.unset(KeyState::Hyper2);
    result.set(KeyState::Hyper);
  }
  return result;
}

} // namespace

Key::Key(const char *name) {
  std::string_view rest(name ? name : "");
  KeyStates states;
  while (true) {
    const auto plus = rest.find('+');
    if (plus == std::string_view::npos || plus + 1 >= rest.size()) {
      break;
    }
    const auto part = rest.substr(0, plus);
    for (const auto &modifier : kModifierNames) {
      if (part == modifier.name) {
        states.set(modifier.state);
      }
    }
    rest.remove_prefix(plus + 1);
  }
  const std::string symName(rest);
  const auto sym = xkb_keysym_from_name(symName.c_str(), XKB_KEYSYM_NO_FLAGS);
  sym_ = sym == XKB_KEY_NoSymbol ? Key_None : static_cast<KeySym>(sym);
  states_ = states;
}

bool Key::check(KeySym sym, KeyStates states) const {
  return sym_ == sym && simpleStates(states_) == simpleStates(states);
}

bool Key::check(const Key &key) const {
  const auto normalized = normalize();
  const auto other = key.normalize();
  return normalized.sym_ == other.sym_ &&
         simpleStates(normalized.states_) == simpleStates(other.states_);
}

bool Key::isModifier() const {
  return (sym_ >= Key_Shift_L && sym_ <= Key_Hyper_R) ||
         (sym_ >= Key_ISO_Lock && sym_ <= Key_ISO_Level5_Lock) ||
         sym_ == Key_Mode_switch || sym_ == Key_Num_Lock;
}

bool Key::isCursorMove() const {
  const auto states = simpleStates(states_);
  const bool allowedStates = states == KeyStates() ||
                             states == KeyStates(KeyState::Ctrl) ||
                             states == KeyStates(KeyState::Shift) ||
                             states == KeyStates(KeyState::Ctrl_Shift);
  if (!allowedStates) {
    return false;
  }
  switch (sym_) {
  case Key_Left:
  case Key_Right:
  case Key_Up:
  case Key_Down:
  case Key_Page_Up:
  case Key_Page_Down:
  case Key_Home:
  case Key_End:
  case Key_KP_Left:
  case Key_KP_Right:
  case Key_KP_Up:
  case Key_KP_Down:
  case Key_KP_Page_Up:
  case Key_KP_Page_Down:
  case Key_KP_Home:
  case Key_KP_End:
    return true;
  default:
    return false;
  }
}

Key Key::normalize() const {
  Key key(*this);
  if (!key.states_.test(KeyState::Shift)) {
    return key;
  }
  // Chữ thường kèm Shift: đổi sang chữ hoa rồi bỏ Shift.
  if (key.sym_ >= Key_a && key.sym_ <= Key_z) {
    key.sym_ = static_cast<KeySym>(key.sym_ - Key_a + Key_A);
    key.states_.unset(KeyState::Shift);
    return key;
  }
  // Ký tự in được (kể cả đã là chữ hoa hay ký hiệu do Shift tạo) không cần
  // Shift để phân biệt. Phím không ra ký tự như Tab, mũi tên vẫn giữ Shift.
  const auto unicode = keySymToUnicode(key.sym_);
  if (unicode > 0x20 && unicode != 0x7F &&
      !(key.sym_ >= Key_KP_Space && key.sym_ <= Key_KP_Equal)) {
    key.states_.unset(KeyState::Shift);
  }
  return key;
}

std::string Key::toString() const {
  std::string result;
  const auto states = simpleStates(states_);
  if (states.test(KeyState::Ctrl)) {
    result += "Control+";
  }
  if (states.test(KeyState::Alt)) {
    result += "Alt+";
  }
  if (states.test(KeyState::Shift)) {
    result += "Shift+";
  }
  if (states.test(KeyState::Super)) {
    result += "Super+";
  }
  if (states.test(KeyState::Hyper)) {
    result += "Hyper+";
  }
  if (states.test(KeyState::Meta)) {
    result += "Meta+";
  }
  char name[64] = {};
  if (sym_ != Key_None && xkb_keysym_get_name(static_cast<xkb_keysym_t>(sym_),
                                              name, sizeof(name)) > 0) {
    result += name;
  }
  return result;
}

uint32_t Key::keySymToUnicode(KeySym sym) {
  return xkb_keysym_to_utf32(static_cast<xkb_keysym_t>(sym));
}

KeySym Key::keySymFromUnicode(uint32_t unicode) {
  return static_cast<KeySym>(xkb_utf32_to_keysym(unicode));
}

std::string Key::keySymToUTF8(KeySym sym) {
  const auto codepoint = keySymToUnicode(sym);
  if (!codepoint) {
    return {};
  }
  return utf8::UCS4ToUTF8(codepoint);
}

} // namespace areca::im
