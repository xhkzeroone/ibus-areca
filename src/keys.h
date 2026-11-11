#pragma once

#include <cstdint>

namespace areca::keys {

// Keysym X11 và mask của IBus. Khai báo lại ở đây để logic xử lý phím không
// cần header IBus và test được trực tiếp.
inline constexpr uint32_t kBackSpace = 0xff08;
inline constexpr uint32_t kTab = 0xff09;
inline constexpr uint32_t kReturn = 0xff0d;
inline constexpr uint32_t kEscape = 0xff1b;
inline constexpr uint32_t kHome = 0xff50;
inline constexpr uint32_t kEnd = 0xff57;
inline constexpr uint32_t kKpTab = 0xff89;
inline constexpr uint32_t kKpEnter = 0xff8d;
inline constexpr uint32_t kKpHome = 0xff95;
inline constexpr uint32_t kKpEnd = 0xff9c;
inline constexpr uint32_t kDelete = 0xffff;
inline constexpr uint32_t kKpDelete = 0xff9f;
inline constexpr uint32_t kIsoLeftTab = 0xfe20;
inline constexpr uint32_t kShiftL = 0xffe1;
inline constexpr uint32_t kHyperR = 0xffee;
inline constexpr uint32_t kIsoLock = 0xfe01;
inline constexpr uint32_t kIsoLevel5Lock = 0xfe13;

inline constexpr uint32_t kShiftMask = 1U << 0;
inline constexpr uint32_t kControlMask = 1U << 2;
inline constexpr uint32_t kMod1Mask = 1U << 3;
inline constexpr uint32_t kSuperMask = 1U << 26;
inline constexpr uint32_t kHyperMask = 1U << 27;
inline constexpr uint32_t kMetaMask = 1U << 28;
inline constexpr uint32_t kReleaseMask = 1U << 30;

inline constexpr uint32_t kShortcutMask =
    kControlMask | kMod1Mask | kSuperMask | kHyperMask | kMetaMask;

inline bool isModifier(uint32_t keyval) {
  return (keyval >= kShiftL && keyval <= kHyperR) ||
         (keyval >= kIsoLock && keyval <= kIsoLevel5Lock);
}

// Home..End gồm mũi tên, Page Up/Down; bản keypad tương ứng cũng vậy.
inline bool isCursorMove(uint32_t keyval) {
  return (keyval >= kHome && keyval <= kEnd) ||
         (keyval >= kKpHome && keyval <= kKpEnd);
}

} // namespace areca::keys
