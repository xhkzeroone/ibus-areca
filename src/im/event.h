#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <glib.h>
#include <time.h>

#include "flags.h"

// Event loop trên GLib main context mặc định, cùng giao diện với event loop
// mà backend Areca dùng.
namespace areca::im {

uint64_t now(clockid_t clock);

enum class IOEventFlag : uint32_t {
  In = 1U << 0,
  Out = 1U << 1,
  Err = 1U << 2,
  Hup = 1U << 3,
};
using IOEventFlags = Flags<IOEventFlag>;

class EventSource {
public:
  virtual ~EventSource();
  EventSource(const EventSource &) = delete;
  EventSource &operator=(const EventSource &) = delete;

  // Chạy một lần rồi tắt. Đây cũng là trạng thái mặc định khi vừa tạo.
  void setOneShot() { setEnabled(true); }
  void setEnabled(bool enabled);
  bool isEnabled() const { return enabled_; }

protected:
  EventSource() = default;
  virtual GSource *createSource() = 0;
  // Gọi từ callback GLib. Trả về true nếu source vẫn cần chạy tiếp.
  template <typename Fn> gboolean dispatch(Fn &&run);
  void attach();
  void detach();

  GSource *source_ = nullptr;
  bool enabled_ = false;
  bool repeat_ = false;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

class EventSourceTime final : public EventSource {
public:
  using Callback = std::function<bool(EventSourceTime *, uint64_t)>;
  EventSourceTime(clockid_t clock, uint64_t usec, Callback callback);

  void setTime(uint64_t usec);
  uint64_t time() const { return time_; }

private:
  friend class EventLoop;
  GSource *createSource() override;
  static gboolean fire(gpointer data);

  clockid_t clock_;
  uint64_t time_;
  Callback callback_;
  bool rescheduled_ = false;
};

class EventSourceIO final : public EventSource {
public:
  using Callback = std::function<bool(EventSourceIO *, int, IOEventFlags)>;
  EventSourceIO(int fd, IOEventFlags flags, Callback callback);

  int fd() const { return fd_; }

private:
  GSource *createSource() override;
  static gboolean fire(gint fd, GIOCondition condition, gpointer data);

  int fd_;
  IOEventFlags flags_;
  Callback callback_;
};

// Chạy khi main loop rảnh, sau các event đang chờ.
class EventSourceIdle final : public EventSource {
public:
  using Callback = std::function<bool(EventSource *)>;
  EventSourceIdle(int priority, Callback callback);

private:
  GSource *createSource() override;
  static gboolean fire(gpointer data);

  int priority_;
  Callback callback_;
};

class EventLoop {
public:
  EventLoop();
  ~EventLoop();
  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;

  std::unique_ptr<EventSourceTime>
  addTimeEvent(clockid_t clock, uint64_t usec, uint64_t accuracy,
               EventSourceTime::Callback callback);
  std::unique_ptr<EventSourceIO> addIOEvent(int fd, IOEventFlags flags,
                                            EventSourceIO::Callback callback);
  // Chạy sau khi các event có sẵn đã được xử lý xong.
  std::unique_ptr<EventSource> addPostEvent(EventSourceIdle::Callback callback);
  // Chạy ở vòng lặp kế tiếp.
  std::unique_ptr<EventSource>
  addDeferEvent(EventSourceIdle::Callback callback);

  // Dùng trong test và chương trình độc lập.
  bool exec();
  void exit();

private:
  GMainLoop *loop_;
};

} // namespace areca::im
