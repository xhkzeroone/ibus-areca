#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "flags.h"
#include "keysym.h"

namespace areca::im {

// Bit trùng modifier mask của X11 và IBus nên state từ IBus dùng thẳng được.
enum class KeyState : uint32_t {
  NoState = 0,
  Shift = 1U << 0,
  CapsLock = 1U << 1,
  Ctrl = 1U << 2,
  Alt = 1U << 3,
  NumLock = 1U << 4,
  Hyper = 1U << 5,
  Super = 1U << 6,
  Mod5 = 1U << 7,
  Super2 = 1U << 26,
  Hyper2 = 1U << 27,
  Meta = 1U << 28,
  // IBus không có cờ lặp phím; bit này do engine tự đặt.
  Repeat = 1U << 31,
  Ctrl_Shift = Ctrl | Shift,
  SimpleMask = Shift | Ctrl | Alt | Super | Super2 | Hyper | Hyper2 | Meta,
};
using KeyStates = Flags<KeyState>;

class Key {
public:
  constexpr Key() = default;
  constexpr explicit Key(KeySym sym, KeyStates states = {}, int code = 0)
      : sym_(sym), states_(states), code_(code) {}
  // Tên dạng "grave", "Control+space" như trong file cấu hình.
  explicit Key(const char *name);
  explicit Key(const std::string &name) : Key(name.c_str()) {}

  KeySym sym() const { return sym_; }
  KeyStates states() const { return states_; }
  int code() const { return code_; }

  bool check(KeySym sym, KeyStates states = {}) const;
  bool check(const Key &key) const;
  template <typename Container> bool checkKeyList(const Container &c) const {
    for (const auto &key : c) {
      if (check(key)) {
        return true;
      }
    }
    return false;
  }

  bool isModifier() const;
  bool isCursorMove() const;
  bool isValid() const { return sym_ != Key_None || code_ != 0; }
  // Bỏ Shift khỏi phím đã ra ký tự in hoa hoặc ký hiệu, để so phím tắt không
  // phụ thuộc cách client báo Shift.
  Key normalize() const;
  std::string toString() const;

  static uint32_t keySymToUnicode(KeySym sym);
  static KeySym keySymFromUnicode(uint32_t unicode);
  static std::string keySymToUTF8(KeySym sym);

  bool operator==(const Key &other) const {
    return sym_ == other.sym_ && states_ == other.states_ &&
           code_ == other.code_;
  }

private:
  KeySym sym_ = Key_None;
  KeyStates states_;
  int code_ = 0;
};

using KeyList = std::vector<Key>;

} // namespace areca::im
