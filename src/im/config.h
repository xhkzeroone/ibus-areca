#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "key.h"
#include "raw_config.h"

// Khung cấu hình khai báo option kèm tên khoá, giá trị mặc định và ràng buộc;
// đọc/ghi file .conf dạng ini.
namespace areca::im {

class Configuration;

class OptionBase {
public:
  OptionBase(Configuration *parent, std::string path, std::string description);
  virtual ~OptionBase() = default;
  OptionBase(const OptionBase &) = delete;
  OptionBase &operator=(const OptionBase &) = delete;

  const std::string &path() const { return path_; }
  const std::string &description() const { return description_; }
  virtual void reset() = 0;
  virtual bool isDefault() const = 0;
  virtual void marshall(RawConfig &config) const = 0;
  // Trả false khi giá trị trong file không hợp lệ; option giữ nguyên giá trị.
  virtual bool unmarshall(const RawConfig &config, bool partial) = 0;
  virtual bool skipSave() const { return false; }

private:
  std::string path_;
  std::string description_;
};

class Configuration {
public:
  Configuration() = default;
  virtual ~Configuration() = default;
  Configuration(const Configuration &) = delete;
  Configuration &operator=(const Configuration &) = delete;

  void load(const RawConfig &config, bool partial = false);
  void save(RawConfig &config) const;
  void addOption(OptionBase *option) { options_.push_back(option); }
  const std::vector<OptionBase *> &options() const { return options_; }

protected:
  void copyFrom(const Configuration &other) {
    RawConfig raw;
    other.save(raw);
    load(raw);
  }

private:
  std::vector<OptionBase *> options_;
};

inline OptionBase::OptionBase(Configuration *parent, std::string path,
                              std::string description)
    : path_(std::move(path)), description_(std::move(description)) {
  parent->addOption(this);
}

struct NoConstrain {
  template <typename T> bool check(const T &) const { return true; }
};

struct IntConstrain {
  IntConstrain(int min = 0, int max = 0) : min_(min), max_(max) {}
  bool check(int value) const { return value >= min_ && value <= max_; }
  int min() const { return min_; }
  int max() const { return max_; }

private:
  int min_;
  int max_;
};

enum class KeyConstrainFlag { None = 0, AllowModifierLess = 1 };
struct KeyConstrain {
  KeyConstrain(KeyConstrainFlag = KeyConstrainFlag::None) {}
  bool check(const Key &) const { return true; }
};
template <typename Sub> struct ListConstrain {
  ListConstrain(Sub sub = {}) : sub_(sub) {}
  template <typename T> bool check(const std::vector<T> &values) const {
    return std::all_of(values.begin(), values.end(),
                       [this](const T &value) { return sub_.check(value); });
  }

private:
  Sub sub_;
};
using KeyListConstrain = ListConstrain<KeyConstrain>;

struct NoAnnotation {};
struct EnumAnnotation {
  void dumpDescription(RawConfig &) const {}
};
struct ListDisplayOptionAnnotation {
  ListDisplayOptionAnnotation(std::string option = {})
      : option_(std::move(option)) {}
  const std::string &option() const { return option_; }

private:
  std::string option_;
};

template <typename T> struct ConfigEnumNames;

namespace detail {

inline void marshallValue(RawConfig &config, const std::string &value) {
  config.setValue(value);
}
inline void marshallValue(RawConfig &config, bool value) {
  config.setValue(value ? "True" : "False");
}
inline void marshallValue(RawConfig &config, int value) {
  config.setValue(std::to_string(value));
}
inline void marshallValue(RawConfig &config, const Key &value) {
  config.setValue(value.toString());
}

inline bool unmarshallValue(const RawConfig &config, std::string &value) {
  value = config.value();
  return true;
}
inline bool unmarshallValue(const RawConfig &config, bool &value) {
  if (config.value() == "True" || config.value() == "true") {
    value = true;
    return true;
  }
  if (config.value() == "False" || config.value() == "false") {
    value = false;
    return true;
  }
  return false;
}
inline bool unmarshallValue(const RawConfig &config, int &value) {
  const auto &text = config.value();
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (*end != '\0') {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}
inline bool unmarshallValue(const RawConfig &config, Key &value) {
  value = Key(config.value());
  return true;
}

template <typename T>
  requires std::is_enum_v<T>
void marshallValue(RawConfig &config, T value) {
  const auto &names = ConfigEnumNames<T>::names();
  const auto index = static_cast<std::size_t>(value);
  config.setValue(index < names.size() ? names[index] : std::string());
}

template <typename T>
  requires std::is_enum_v<T>
bool unmarshallValue(const RawConfig &config, T &value) {
  const auto &names = ConfigEnumNames<T>::names();
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (names[i] == config.value()) {
      value = static_cast<T>(i);
      return true;
    }
  }
  return false;
}

template <typename T>
  requires std::is_base_of_v<Configuration, T>
void marshallValue(RawConfig &config, const T &value) {
  value.save(config);
}

template <typename T>
  requires std::is_base_of_v<Configuration, T>
bool unmarshallValue(const RawConfig &config, T &value) {
  value.load(config);
  return true;
}

// Danh sách lưu thành các nhánh con "0", "1", ...
template <typename T>
void marshallValue(RawConfig &config, const std::vector<T> &values) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    marshallValue(*config.get(std::to_string(i), true), values[i]);
  }
}

template <typename T>
bool unmarshallValue(const RawConfig &config, std::vector<T> &values) {
  values.clear();
  for (std::size_t i = 0;; ++i) {
    const auto *item = config.get(std::to_string(i));
    if (!item) {
      break;
    }
    T value{};
    if (!unmarshallValue(*item, value)) {
      return false;
    }
    values.push_back(std::move(value));
  }
  return true;
}

} // namespace detail

template <typename T, typename Constrain = NoConstrain,
          typename Annotation = NoAnnotation>
class Option : public OptionBase {
public:
  Option(Configuration *parent, std::string path, std::string description,
         T defaultValue = {}, Constrain constrain = {}, NoAnnotation = {},
         Annotation annotation = {})
      : OptionBase(parent, std::move(path), std::move(description)),
        defaultValue_(defaultValue), value_(std::move(defaultValue)),
        constrain_(std::move(constrain)), annotation_(std::move(annotation)) {}

  const T &value() const { return value_; }
  const T &defaultValue() const { return defaultValue_; }
  const T &operator*() const { return value_; }
  T *mutableValue() { return &value_; }
  bool setValue(T value) {
    if (!constrain_.check(value)) {
      return false;
    }
    value_ = std::move(value);
    return true;
  }
  Annotation &annotation() { return annotation_; }
  const Annotation &annotation() const { return annotation_; }
  const Constrain &constrain() const { return constrain_; }

  void reset() override { value_ = defaultValue_; }
  bool isDefault() const override {
    RawConfig current;
    RawConfig fallback;
    detail::marshallValue(current, value_);
    detail::marshallValue(fallback, defaultValue_);
    return writeIniText(current) == writeIniText(fallback) &&
           current.value() == fallback.value();
  }
  void marshall(RawConfig &config) const override {
    detail::marshallValue(config, value_);
  }
  bool unmarshall(const RawConfig &config, bool partial) override {
    T parsed = partial ? value_ : defaultValue_;
    if (!detail::unmarshallValue(config, parsed) || !constrain_.check(parsed)) {
      return false;
    }
    value_ = std::move(parsed);
    return true;
  }

private:
  T defaultValue_;
  T value_;
  Constrain constrain_;
  Annotation annotation_;
};

// Option không hiện trong giao diện cài đặt nhưng vẫn đọc/ghi file.
template <typename T, typename Constrain = NoConstrain>
using HiddenOption = Option<T, Constrain>;

template <typename T, typename Annotation>
class OptionWithAnnotation : public Option<T, NoConstrain, Annotation> {
public:
  OptionWithAnnotation(Configuration *parent, std::string path,
                       std::string description, T defaultValue = {},
                       NoConstrain constrain = {}, NoAnnotation marshaller = {},
                       Annotation annotation = {})
      : Option<T, NoConstrain, Annotation>(parent, std::move(path),
                                           std::move(description),
                                           std::move(defaultValue), constrain,
                                           marshaller, std::move(annotation)) {}
};

using KeyListOption = Option<KeyList, KeyListConstrain>;

inline void Configuration::load(const RawConfig &config, bool partial) {
  for (auto *option : options_) {
    const auto *sub = config.get(option->path());
    if (!sub) {
      if (!partial) {
        option->reset();
      }
      continue;
    }
    if (!option->unmarshall(*sub, partial) && !partial) {
      option->reset();
    }
  }
}

inline void Configuration::save(RawConfig &config) const {
  for (const auto *option : options_) {
    if (option->skipSave()) {
      continue;
    }
    auto *sub = config.get(option->path(), true);
    option->marshall(*sub);
    sub->setComment(option->description());
    sub->setCommentedOut(!sub->hasSubItems() && option->isDefault());
  }
}

} // namespace areca::im

// Khai báo một struct cấu hình. Copy chép giá trị qua RawConfig vì mỗi option
// tự đăng ký vào đúng object chứa nó.
#define ARECA_CONFIGURATION(NAME, ...)                                         \
  struct NAME : public ::areca::im::Configuration {                            \
    NAME() = default;                                                          \
    NAME(const NAME &other) : ::areca::im::Configuration() {                   \
      copyFrom(other);                                                         \
    }                                                                          \
    NAME &operator=(const NAME &other) {                                       \
      if (this != &other) {                                                    \
        copyFrom(other);                                                       \
      }                                                                        \
      return *this;                                                            \
    }                                                                          \
    __VA_ARGS__                                                                \
  };

// Tên lưu trong file của từng giá trị enum, theo đúng thứ tự khai báo.
#define ARECA_CONFIG_ENUM_NAME(TYPE, ...)                                      \
  template <> struct areca::im::ConfigEnumNames<TYPE> {                        \
    static const std::vector<std::string> &names() {                           \
      static const std::vector<std::string> values{__VA_ARGS__};               \
      return values;                                                           \
    }                                                                          \
  };
