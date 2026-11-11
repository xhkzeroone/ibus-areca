#pragma once

#include <sstream>
#include <string>

#include <glib.h>

namespace areca {

// Gom một dòng log rồi ghi một lần khi hết biểu thức để các phần ghép bằng <<
// không bị xen giữa.
class LogLine final {
public:
  enum class Level { Info, Warn, Error };
  explicit LogLine(Level level) : level_(level) {}
  ~LogLine() {
    const auto text = stream_.str();
    switch (level_) {
    case Level::Info:
      g_message("%s", text.c_str());
      break;
    case Level::Warn:
    case Level::Error:
      g_warning("%s", text.c_str());
      break;
    }
  }
  LogLine(const LogLine &) = delete;
  LogLine &operator=(const LogLine &) = delete;

  template <typename T> LogLine &operator<<(const T &value) {
    stream_ << value;
    return *this;
  }

private:
  Level level_;
  std::ostringstream stream_;
};

} // namespace areca

#define ARECA_INFO() ::areca::LogLine(::areca::LogLine::Level::Info)
#define ARECA_WARN() ::areca::LogLine(::areca::LogLine::Level::Warn)
#define ARECA_ERROR() ::areca::LogLine(::areca::LogLine::Level::Error)
