#include "event.h"

#include <glib-unix.h>

namespace areca::im {

uint64_t now(clockid_t clock) {
  timespec ts{};
  clock_gettime(clock, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
         static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

EventSource::~EventSource() {
  *alive_ = false;
  detach();
}

void EventSource::setEnabled(bool enabled) {
  if (enabled == enabled_) {
    return;
  }
  enabled_ = enabled;
  if (enabled) {
    attach();
  } else {
    detach();
  }
}

void EventSource::attach() {
  detach();
  source_ = createSource();
  g_source_attach(source_, nullptr);
}

void EventSource::detach() {
  if (source_) {
    g_source_destroy(source_);
    g_source_unref(source_);
    source_ = nullptr;
  }
}

template <typename Fn> gboolean EventSource::dispatch(Fn &&run) {
  // Callback có thể huỷ chính source này hoặc gắn lại nó; chỉ đụng vào object
  // khi nó còn sống và vẫn đang giữ đúng GSource vừa chạy.
  auto alive = alive_;
  GSource *firing = source_;
  const bool keep = run();
  if (!*alive) {
    return G_SOURCE_REMOVE;
  }
  if (source_ != firing) {
    return G_SOURCE_REMOVE;
  }
  if (keep && repeat_) {
    return G_SOURCE_CONTINUE;
  }
  g_source_unref(source_);
  source_ = nullptr;
  enabled_ = false;
  return G_SOURCE_REMOVE;
}

EventSourceTime::EventSourceTime(clockid_t clock, uint64_t usec,
                                 Callback callback)
    : clock_(clock), time_(usec), callback_(std::move(callback)) {}

void EventSourceTime::setTime(uint64_t usec) {
  time_ = usec;
  if (enabled_) {
    // Gắn lại GSource mới; dispatch thấy source đổi nên giữ timer bật.
    attach();
  } else {
    enabled_ = true;
    attach();
  }
}

GSource *EventSourceTime::createSource() {
  const uint64_t current = now(clock_);
  const uint64_t delayUsec = time_ > current ? time_ - current : 0;
  // GLib tính theo ms; làm tròn lên để không chạy sớm hơn hạn.
  const guint delayMs = static_cast<guint>((delayUsec + 999) / 1000);
  GSource *source = g_timeout_source_new(delayMs);
  g_source_set_priority(source, G_PRIORITY_DEFAULT);
  g_source_set_callback(source, &EventSourceTime::fire, this, nullptr);
  return source;
}

gboolean EventSourceTime::fire(gpointer data) {
  auto *self = static_cast<EventSourceTime *>(data);
  return self->dispatch(
      [self]() { return self->callback_(self, now(self->clock_)); });
}

EventSourceIO::EventSourceIO(int fd, IOEventFlags flags, Callback callback)
    : fd_(fd), flags_(flags), callback_(std::move(callback)) {
  repeat_ = true;
}

GSource *EventSourceIO::createSource() {
  int condition = 0;
  if (flags_.test(IOEventFlag::In)) {
    condition |= G_IO_IN | G_IO_PRI;
  }
  if (flags_.test(IOEventFlag::Out)) {
    condition |= G_IO_OUT;
  }
  if (flags_.test(IOEventFlag::Err)) {
    condition |= G_IO_ERR;
  }
  if (flags_.test(IOEventFlag::Hup)) {
    condition |= G_IO_HUP;
  }
  GSource *source =
      g_unix_fd_source_new(fd_, static_cast<GIOCondition>(condition));
  g_source_set_callback(source, G_SOURCE_FUNC(&EventSourceIO::fire), this,
                        nullptr);
  return source;
}

gboolean EventSourceIO::fire(gint fd, GIOCondition condition, gpointer data) {
  auto *self = static_cast<EventSourceIO *>(data);
  IOEventFlags flags;
  if (condition & (G_IO_IN | G_IO_PRI)) {
    flags.set(IOEventFlag::In);
  }
  if (condition & G_IO_OUT) {
    flags.set(IOEventFlag::Out);
  }
  if (condition & (G_IO_ERR | G_IO_NVAL)) {
    flags.set(IOEventFlag::Err);
  }
  if (condition & G_IO_HUP) {
    flags.set(IOEventFlag::Hup);
  }
  return self->dispatch(
      [self, fd, flags]() { return self->callback_(self, fd, flags); });
}

EventSourceIdle::EventSourceIdle(int priority, Callback callback)
    : priority_(priority), callback_(std::move(callback)) {}

GSource *EventSourceIdle::createSource() {
  GSource *source = g_idle_source_new();
  g_source_set_priority(source, priority_);
  g_source_set_callback(source, &EventSourceIdle::fire, this, nullptr);
  return source;
}

gboolean EventSourceIdle::fire(gpointer data) {
  auto *self = static_cast<EventSourceIdle *>(data);
  return self->dispatch([self]() { return self->callback_(self); });
}

EventLoop::EventLoop() : loop_(g_main_loop_new(nullptr, FALSE)) {}

EventLoop::~EventLoop() { g_main_loop_unref(loop_); }

std::unique_ptr<EventSourceTime>
EventLoop::addTimeEvent(clockid_t clock, uint64_t usec, uint64_t,
                        EventSourceTime::Callback callback) {
  auto source =
      std::make_unique<EventSourceTime>(clock, usec, std::move(callback));
  source->setEnabled(true);
  return source;
}

std::unique_ptr<EventSourceIO>
EventLoop::addIOEvent(int fd, IOEventFlags flags,
                      EventSourceIO::Callback callback) {
  auto source = std::make_unique<EventSourceIO>(fd, flags, std::move(callback));
  source->setEnabled(true);
  return source;
}

std::unique_ptr<EventSource>
EventLoop::addPostEvent(EventSourceIdle::Callback callback) {
  auto source = std::make_unique<EventSourceIdle>(G_PRIORITY_DEFAULT_IDLE,
                                                  std::move(callback));
  source->setEnabled(true);
  return source;
}

std::unique_ptr<EventSource>
EventLoop::addDeferEvent(EventSourceIdle::Callback callback) {
  auto source = std::make_unique<EventSourceIdle>(G_PRIORITY_DEFAULT,
                                                  std::move(callback));
  source->setEnabled(true);
  return source;
}

bool EventLoop::exec() {
  g_main_loop_run(loop_);
  return true;
}

void EventLoop::exit() { g_main_loop_quit(loop_); }

} // namespace areca::im
