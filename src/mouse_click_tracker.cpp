#include "mouse_click_tracker.h"

#include "im/im.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace areca {
namespace {

constexpr uint64_t kReapRetryUsec = 20 * 1000;
constexpr uint64_t kBackoffBaseUsec = 1000 * 1000;
constexpr uint64_t kBackoffMaxUsec = 5 * 60 * 1000 * 1000ULL;
// Helper chạy ổn lâu hơn mốc này rồi mới chết thì không tính là lỗi liên tục.
constexpr uint64_t kStableRunUsec = 60 * 1000 * 1000ULL;

bool tryReap(pid_t pid) {
  pid_t result;
  do {
    result = waitpid(pid, nullptr, WNOHANG);
  } while (result < 0 && errno == EINTR);
  // ECHILD: tiến trình không còn là con, không còn gì để thu hồi.
  return result == pid || (result < 0 && errno == ECHILD);
}

// Không được waitpid chặn trên event loop. Chưa thu hồi được thì timer tự giữ
// chính nó thử lại tới khi xong, vì tracker có thể bị huỷ ngay sau stop().
void reapLater(im::EventLoop &loop, pid_t pid) {
  if (tryReap(pid))
    return;
  auto timer = std::make_shared<std::unique_ptr<im::EventSourceTime>>();
  *timer = loop.addTimeEvent(
      CLOCK_MONOTONIC, im::now(CLOCK_MONOTONIC) + kReapRetryUsec, 0,
      [timer, pid](im::EventSourceTime *source, uint64_t now) {
        if (!tryReap(pid)) {
          source->setTime(now + kReapRetryUsec);
          return true;
        }
        // Huỷ timer sau khi callback xong; đây là tham chiếu cuối giữ nó.
        auto completed = std::move(*timer);
        return false;
      });
}

} // namespace

MouseClickTracker::~MouseClickTracker() { stop(); }

bool MouseClickTracker::start() {
  // activate có thể gọi nhiều lần; chỉ tạo lại helper khi pipe cũ đã đóng.
  if (fd_ >= 0)
    return true;
  stop();
  // Helper vừa lỗi thì chưa spawn lại, tránh mỗi lần đổi focus một lần spawn.
  if (im::now(CLOCK_MONOTONIC) < retryAfterUsec_) {
    if (debugEnabled())
      ARECA_INFO() << "areca: mouse helper start skipped; backoff";
    return false;
  }
  // Pipe riêng cho cặp cha/con, không cần địa chỉ socket hay xác thực peer.
  // NONBLOCK tránh treo event loop; CLOEXEC tránh truyền fd thừa sang helper.
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) < 0) {
    ARECA_WARN() << "areca: mouse tracker pipe failed: "
                 << std::strerror(errno);
    return false;
  }
  posix_spawn_file_actions_t actions;
  const int initError = posix_spawn_file_actions_init(&actions);
  if (initError != 0) {
    ARECA_WARN() << "areca: mouse tracker spawn setup failed: "
                 << std::strerror(initError);
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }
  // Chỉ stdout mang protocol R/C; stderr giữ riêng để log không lẫn vào dữ
  // liệu. posix_spawn chạy helper theo đường dẫn cài đặt, không qua shell.
  int error =
      posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  char path[] = ARECA_MOUSE_HELPER_PATH;
  char *argv[] = {path, nullptr};
  if (!error)
    error = posix_spawn(&child_, path, &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  // Cha không giữ đầu ghi để read nhận EOF khi helper thoát.
  close(pipefd[1]);
  if (error) {
    ARECA_WARN() << "areca: mouse helper spawn failed path=" << path
                 << " error=" << std::strerror(error);
    close(pipefd[0]);
    child_ = -1;
    recordFailure();
    return false;
  }
  fd_ = pipefd[0];
  startedUsec_ = im::now(CLOCK_MONOTONIC);
  // Nhận click trên cùng luồng xử lý phím để không sửa trạng thái engine từ
  // thread khác. Callback chỉ đánh dấu, chưa reset khi rewrite còn dang dở.
  source_ = loop_.addIOEvent(
      fd_, im::IOEventFlags{im::IOEventFlag::In, im::IOEventFlag::Hup},
      [this](im::EventSourceIO *, int, im::IOEventFlags) {
        drain();
        return true;
      });
  if (!source_) {
    ARECA_WARN() << "areca: mouse tracker cannot watch pipe";
    stop();
    return false;
  }
  if (debugEnabled())
    ARECA_INFO() << "areca: mouse helper started pid=" << child_;
  return true;
}

void MouseClickTracker::stop() {
  // Gỡ watcher trước khi đóng fd để event loop không truy cập fd đã tái sử
  // dụng.
  source_.reset();
  if (fd_ >= 0)
    close(fd_);
  fd_ = -1;
  terminateChild();
  ready_ = pending_ = false;
}

void MouseClickTracker::terminateChild() {
  if (child_ > 0) {
    if (debugEnabled())
      ARECA_INFO() << "areca: mouse helper cleanup pid=" << child_;
    // Helper không có dữ liệu cần lưu; kết thúc dứt điểm và thu hồi để không
    // để lại zombie khi addon bị hủy hoặc khởi động lại helper.
    kill(child_, SIGKILL);
    reapLater(loop_, child_);
  }
  child_ = -1;
}

void MouseClickTracker::recordFailure() {
  const uint64_t now = im::now(CLOCK_MONOTONIC);
  if (startedUsec_ && now - startedUsec_ >= kStableRunUsec)
    failures_ = 0;
  startedUsec_ = 0;
  const uint64_t delay =
      std::min(kBackoffBaseUsec << std::min(failures_, 16U), kBackoffMaxUsec);
  ++failures_;
  retryAfterUsec_ = now + delay;
  ARECA_WARN() << "areca: mouse helper failures=" << failures_
               << " retry_after_ms=" << delay / 1000;
}

void MouseClickTracker::drain() {
  if (fd_ < 0)
    return;
  char bytes[256];
  for (;;) {
    const auto count = read(fd_, bytes, sizeof(bytes));
    if (count > 0) {
      for (ssize_t i = 0; i < count; ++i) {
        // R: helper khởi tạo xong; C: có nhấn nút chuột. Nhiều C gộp thành một
        // lần reset vì mục đích là bỏ composition cũ, không đếm số lần click.
        if (bytes[i] == 'R') {
          ready_ = true;
          if (debugEnabled())
            ARECA_INFO() << "areca: mouse helper ready pid=" << child_;
        }
        if (bytes[i] == 'C') {
          if (!pending_ && debugEnabled())
            ARECA_INFO() << "areca: mouse click received; reset pending";
          pending_ = true;
        }
      }
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        if (count == 0) {
          ARECA_WARN() << "areca: mouse helper disconnected pid=" << child_;
        } else {
          ARECA_WARN() << "areca: mouse tracker read failed: "
                       << std::strerror(errno);
        }
        // Chỉ tắt watcher vì drain có thể đang chạy ngay trong callback của nó.
        // Giữ pending_ để click đã nhận vẫn được xử lý dù helper mất kết nối.
        if (source_)
          source_->setEnabled(false);
        close(fd_);
        fd_ = -1;
        ready_ = false;
        // Thu hồi ngay thay vì để zombie tới lần activate sau.
        terminateChild();
        recordFailure();
      }
      return;
    }
  }
}

bool MouseClickTracker::hasPendingClick() {
  // Phím và pipe có thể cùng sẵn sàng nhưng callback phím chạy trước. Đọc
  // thêm ở đây để lấy click đã đến pipe trước khi quyết định xử lý phím mới.
  drain();
  return pending_;
}

} // namespace areca
