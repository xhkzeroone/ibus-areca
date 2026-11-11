#pragma once

#include <initializer_list>
#include <type_traits>

namespace areca::im {

// Tập cờ trên một enum class, thao tác bằng test/set như bitset.
template <typename Enum> class Flags {
public:
  using Storage = std::underlying_type_t<Enum>;

  constexpr Flags() = default;
  constexpr Flags(Enum value) : value_(static_cast<Storage>(value)) {}
  constexpr Flags(std::initializer_list<Enum> values) {
    for (auto value : values) {
      value_ |= static_cast<Storage>(value);
    }
  }
  static constexpr Flags fromInteger(Storage value) {
    Flags flags;
    flags.value_ = value;
    return flags;
  }

  constexpr bool test(Enum value) const {
    const auto bits = static_cast<Storage>(value);
    return (value_ & bits) == bits;
  }
  constexpr bool testAny(Flags other) const {
    return (value_ & other.value_) != 0;
  }
  constexpr Flags &set(Enum value, bool enable = true) {
    if (enable) {
      value_ |= static_cast<Storage>(value);
    } else {
      value_ &= ~static_cast<Storage>(value);
    }
    return *this;
  }
  constexpr Flags &unset(Enum value) { return set(value, false); }
  constexpr Storage toInteger() const { return value_; }

  constexpr Flags operator|(Flags other) const {
    return fromInteger(value_ | other.value_);
  }
  constexpr Flags operator&(Flags other) const {
    return fromInteger(value_ & other.value_);
  }
  constexpr Flags &operator|=(Flags other) {
    value_ |= other.value_;
    return *this;
  }
  constexpr Flags &operator&=(Flags other) {
    value_ &= other.value_;
    return *this;
  }
  constexpr Flags operator~() const { return fromInteger(~value_); }
  constexpr bool operator==(const Flags &other) const = default;

private:
  Storage value_ = 0;
};

} // namespace areca::im
