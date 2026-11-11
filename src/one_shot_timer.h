#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include <glib.h>

namespace areca {

// Timer chạy một lần trên GLib main context mặc định. Huỷ khi bị xoá để
// callback không chạy trên object đã mất.
class OneShotTimer final {
public:
  OneShotTimer() = default;
  ~OneShotTimer() { cancel(); }
  OneShotTimer(const OneShotTimer &) = delete;
  OneShotTimer &operator=(const OneShotTimer &) = delete;

  void start(uint32_t delayMs, std::function<void()> callback) {
    cancel();
    callback_ = std::move(callback);
    sourceId_ = g_timeout_add(delayMs, &OneShotTimer::fire, this);
  }

  void cancel() {
    if (sourceId_) {
      g_source_remove(sourceId_);
      sourceId_ = 0;
    }
    callback_ = {};
  }

  bool active() const { return sourceId_ != 0; }

private:
  static gboolean fire(gpointer data) {
    auto *self = static_cast<OneShotTimer *>(data);
    // Callback có thể start lại chính timer này, nên tách state ra trước.
    self->sourceId_ = 0;
    auto callback = std::move(self->callback_);
    self->callback_ = {};
    if (callback) {
      callback();
    }
    return G_SOURCE_REMOVE;
  }

  guint sourceId_ = 0;
  std::function<void()> callback_;
};

inline uint64_t monotonicNowUsec() {
  return static_cast<uint64_t>(g_get_monotonic_time());
}

} // namespace areca
