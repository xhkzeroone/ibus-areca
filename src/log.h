#pragma once

#include <sstream>
#include <string>

#include <glib.h>

namespace areca {

// Gom một dòng log rồi ghi một lần khi hết biểu thức để các phần ghép bằng <<
// không bị xen giữa.
class LogLine final {
public:
  explicit LogLine(bool error) : error_(error) {}
  ~LogLine() {
    const auto text = stream_.str();
    if (error_) {
      g_warning("%s", text.c_str());
    } else {
      g_message("%s", text.c_str());
    }
  }
  LogLine(const LogLine &) = delete;
  LogLine &operator=(const LogLine &) = delete;

  template <typename T> LogLine &operator<<(const T &value) {
    stream_ << value;
    return *this;
  }

private:
  bool error_;
  std::ostringstream stream_;
};

} // namespace areca

#define ARECA_INFO() ::areca::LogLine(false)
#define ARECA_ERROR() ::areca::LogLine(true)
