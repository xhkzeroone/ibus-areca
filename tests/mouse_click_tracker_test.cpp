#include "mouse_click_tracker.h"

#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <glib.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Không tự reap: WNOWAIT chỉ nhìn, để kiểm tra chính tracker đã thu hồi con.
bool noChildLeft() {
  siginfo_t info{};
  return waitid(P_ALL, 0, &info, WEXITED | WNOHANG | WNOWAIT) == -1 &&
         errno == ECHILD;
}

// Chạy event loop từng nhịp ngắn tới khi tracker thu hồi hết helper.
bool waitReaped(areca::MouseClickTracker *tracker = nullptr) {
  for (int i = 0; i < 2000; ++i) {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    if (tracker)
      tracker->hasPendingClick();
    if (noChildLeft())
      return true;
    usleep(1000);
  }
  return false;
}

} // namespace

int main() {
  areca::im::EventLoop loop;
  areca::MouseClickTracker tracker(loop);
  for (int run = 0; run < 3; ++run) {
    assert(tracker.start());
    assert(tracker.start());
    for (int i = 0; i < 1000 && !tracker.hasPendingClick(); ++i)
      usleep(1000);
    assert(tracker.isValid());
    assert(tracker.hasPendingClick());
    // Reading again must preserve a reset deferred by the scheduler.
    assert(tracker.hasPendingClick());
    tracker.clearPendingClick();
    assert(!tracker.hasPendingClick());
    tracker.stop();
    assert(!tracker.isValid());
    assert(!tracker.hasPendingClick());
    assert(waitReaped());
  }
  // Tắt cấu hình hủy unique_ptr khi click vẫn pending: phải thu hồi helper,
  // không còn watcher hoặc callback truy cập tracker đã bị hủy.
  {
    auto enabled = std::make_unique<areca::MouseClickTracker>(loop);
    assert(enabled->start());
    for (int i = 0; i < 1000 && !enabled->hasPendingClick(); ++i)
      usleep(1000);
    assert(enabled->hasPendingClick());
    enabled.reset();
    assert(waitReaped());
  }
  // Helper thoát ngay: tracker tự thu hồi, rồi chặn spawn lại trong 1 s đầu.
  {
    areca::MouseClickTracker failing(loop);
    setenv("ARECA_TEST_HELPER_EXIT", "1", 1);
    assert(failing.start());
    assert(waitReaped(&failing));
    assert(!failing.start());
    assert(noChildLeft());
    unsetenv("ARECA_TEST_HELPER_EXIT");
    usleep(1100 * 1000);
    assert(failing.start());
    failing.stop();
    assert(waitReaped());
  }
}
