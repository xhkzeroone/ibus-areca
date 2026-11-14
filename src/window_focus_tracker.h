#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "x11_focus_probe.h"

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;

namespace areca {

class WindowFocusTracker {
public:
  explicit WindowFocusTracker(bool debugEnabled = false);
  ~WindowFocusTracker();

  WindowFocusTracker(const WindowFocusTracker &) = delete;
  WindowFocusTracker &operator=(const WindowFocusTracker &) = delete;

  bool start();
  void stop();
  void requestAccessibilityPoke() {
    pokeRequested_.store(true, std::memory_order_relaxed);
  }
  void setDebugEnabled(bool enabled) {
    debugEnabled_.store(enabled, std::memory_order_relaxed);
  }

  bool isValid() const { return valid_.load(std::memory_order_relaxed); }

  struct BrowserFocus {
    std::string program;
    std::string className;
  };

  BrowserFocus browserFocus() const {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    if (!x11WineProgram_.empty()) {
      return {x11WineProgram_, {}};
    }
    return {focusProgram_, focusClass_};
  }

  bool isTerminalFocused() const {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    return x11WineProgram_.empty() &&
           focusInTerminal_.load(std::memory_order_relaxed);
  }

  // Wine không báo focus qua AT-SPI nên tên từ AT-SPI có thể là của app trước.
  // Khi X11 báo cửa sổ Wine đang focus thì tên đó đúng hơn.
  std::string focusProgram() const {
    std::lock_guard<std::mutex> lock(focusProgramMutex_);
    return x11WineProgram_.empty() ? focusProgram_ : x11WineProgram_;
  }

private:
  friend struct WindowFocusTrackerTestAccess;
  void connectionEstablished();
  void connectionLost();
  void reportConnectionUnavailable(const char *reason);
  void clearFocus();
  void loseFocus(const char *sender, const char *path = nullptr);
  struct PokeSchedule;
  // Focus mới nhất trong một lượt đọc message; chỉ lần cuối được hỏi chi tiết.
  struct PendingFocus {
    std::string sender;
    std::string path;
    bool pending = false;
  };
  bool registerEvents(DBusConnection *bus);
  void addEventMatches(DBusConnection *bus);
  void runConnection(DBusConnection *bus);
  void runEventLoop(DBusConnection *bus);
  void processMessage(DBusMessage *message, PokeSchedule &schedule,
                      PendingFocus &pendingFocus);
  bool processStateChanged(DBusMessage *message, PendingFocus &pendingFocus);
  void processNameOwnerChanged(DBusMessage *message, PokeSchedule &schedule,
                               PendingFocus &pendingFocus);
  void processFocusEvent(DBusConnection *bus, const std::string &sender,
                         const std::string &path);
  void pollX11Focus();
  void waitBeforeReconnect(int intervals);

  static constexpr int kReadTimeoutMs = 100;
  void threadFunc();

  // Owned by the monitor thread.
  std::string focusBus_;
  std::string focusPath_;

  std::thread thread_;
  std::atomic<bool> valid_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> pokeRequested_{false};
  std::atomic<bool> debugEnabled_{false};
  std::atomic<bool> connectionFailureReported_{false};
  std::atomic<bool> focusInTerminal_{false};

  mutable std::mutex focusProgramMutex_;
  std::string focusProgram_;
  std::string focusClass_;
  // Chỉ có giá trị khi cửa sổ X11 đang focus là tiến trình Wine.
  std::string x11WineProgram_;
  // Thuộc thread theo dõi.
  X11FocusProbe x11Probe_;
};

} // namespace areca
