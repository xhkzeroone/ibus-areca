#include <cassert>
#include <string>

#include "im/im.h"

#include "input_type_detector.h"
#include "window_focus_tracker.h"

namespace areca {

struct WindowFocusTrackerTestAccess {
  static void seedFocus(WindowFocusTracker &tracker, const std::string &bus,
                        const std::string &path, const std::string &program,
                        bool terminal) {
    tracker.valid_.store(true);
    tracker.focusBus_ = bus;
    tracker.focusPath_ = path;
    tracker.focusInTerminal_.store(terminal);
    {
      std::lock_guard<std::mutex> lock(tracker.focusProgramMutex_);
      tracker.focusProgram_ = program;
      tracker.focusClass_.clear();
    }
  }

  static void setClass(WindowFocusTracker &tracker,
                       const std::string &className) {
    std::lock_guard<std::mutex> lock(tracker.focusProgramMutex_);
    tracker.focusClass_ = className;
  }

  static void loseFocus(WindowFocusTracker &tracker, const char *sender,
                        const char *path = nullptr) {
    tracker.loseFocus(sender, path);
  }

  static void connectionLost(WindowFocusTracker &tracker) {
    tracker.connectionLost();
  }

  static void connectionEstablished(WindowFocusTracker &tracker) {
    tracker.connectionEstablished();
  }
};

} // namespace areca

class TestInputContext : public areca::im::InputContext {
public:
  TestInputContext(areca::im::InputContextManager &manager,
                   const std::string &program)
      : areca::im::InputContext(manager, program) {}
  ~TestInputContext() override { destroy(); }

  void commitStringImpl(const std::string &) override {}
  void deleteSurroundingTextImpl(int, unsigned int) override {}
  void forwardKeyImpl(const areca::im::ForwardKeyEvent &) override {}
  void updatePreeditImpl() override {}
};

int main() {
  using areca::InputTypeDetector;
  using areca::WindowFocusTracker;

  // Case 1: Null tracker
  {
    InputTypeDetector detector(nullptr);

    // Terminal by program name
    assert(detector.isTerminal("ghostty"));
    assert(detector.isTerminal("com.mitchellh.ghostty"));
    assert(detector.isTerminal("mate-terminal"));

    // Not a terminal program
    assert(!detector.isTerminal("firefox"));
    assert(!detector.isTerminal("code"));
    assert(!detector.isTerminal(""));
    assert(!detector.isTerminal(""));

    // Browser checks with null tracker
    assert(detector.isBrowser("google-chrome"));
    assert(detector.isBrowser("firefox"));
    assert(detector.isBrowser("thorium"));
    assert(detector.isBrowser("ladybird"));
    assert(!detector.isBrowser("code"));
    assert(!detector.isBrowser("kate"));
    assert(!detector.isBrowser("postman"));
    assert(!detector.isBrowser("redisinsight"));
    assert(!detector.isBrowser("slack"));
    assert(detector.isBrowser(""));

    // Chromium browser checks with null tracker
    assert(detector.isChromiumBrowser("google-chrome"));
    assert(detector.isChromiumBrowser("chromium"));
    assert(detector.isChromiumBrowser("brave"));
    assert(detector.isChromiumBrowser("microsoft-edge"));
    assert(!detector.isChromiumBrowser("firefox"));
    assert(!detector.isChromiumBrowser("code"));
    assert(!detector.isChromiumBrowser(""));
  }

  // Case 2: Inactive/invalid tracker (SPI disabled)
  {
    WindowFocusTracker tracker;
    assert(!tracker.isValid());
    InputTypeDetector detector(&tracker);

    // Browser checks with inactive tracker
    assert(detector.isBrowser("google-chrome"));
    assert(detector.isBrowser("firefox"));
    assert(!detector.isBrowser("code"));
    assert(!detector.isBrowser("postman"));
    assert(detector.isBrowser(""));

    // Terminal checks with inactive tracker
    assert(!detector.isTerminal("firefox"));
    assert(!detector.isTerminal("code"));
    assert(detector.isTerminal("ghostty"));
    assert(detector.isTerminal("gnome-terminal-server"));
    assert(!detector.isTerminal(""));
  }

  // Case 3: Setter wiring
  {
    InputTypeDetector detector;
    WindowFocusTracker tracker;

    detector.setFocusTracker(&tracker);

    detector.setFocusTracker(nullptr);
  }

  // Chromium variants can add prefixes or suffixes to the omnibox class.
  {
    areca::im::InputContextManager manager;
    TestInputContext edge(manager, "microsoft-edge");
    edge.setCapabilityFlags(areca::im::CapabilityFlag::Url);
    InputTypeDetector detector;
    // Url is authoritative and does not depend on AT-SPI availability.
    assert(detector.inChromiumAddressBar(edge));

    edge.setCapabilityFlags(areca::im::CapabilityFlags{});
    assert(!detector.inChromiumAddressBar(edge));

    WindowFocusTracker tracker;
    detector.setFocusTracker(&tracker);
    areca::WindowFocusTrackerTestAccess::seedFocus(tracker, ":1.53", "/omnibox",
                                                   "msedge", false);
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    assert(detector.inChromiumAddressBar(edge));
    // The Url capability is not needed for a match.
    edge.setCapabilityFlags(areca::im::CapabilityFlags{});
    assert(detector.inChromiumAddressBar(edge));

    // Page inputs must not match the current Omnibox substring rule.
    for (const auto *className : {"control   ", ""}) {
      areca::WindowFocusTrackerTestAccess::setClass(tracker, className);
      assert(!detector.inChromiumAddressBar(edge));
    }
    for (const auto *className :
         {"OmniboxResultView", "BraveOmniboxViewViews",
          "OmniboxViewViews extra", "CustomOmniboxViewViewsSubclass"}) {
      areca::WindowFocusTrackerTestAccess::setClass(tracker, className);
      assert(detector.inChromiumAddressBar(edge));
    }
    // Re-entering the page immediately clears the previous verdict.
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    assert(detector.inChromiumAddressBar(edge));
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.53", "/page-input", "msedge", false);
    assert(!detector.inChromiumAddressBar(edge));

    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    // All browser identities use the existing Chromium list, without aliases.
    for (const auto *program :
         {"microsoft-edge-stable", "msedge", "com.microsoft.Edge.desktop",
          "brave-browser", "brave", "google-chrome-beta", "chromium-browser"}) {
      TestInputContext alias(manager, program);
      assert(detector.inChromiumAddressBar(alias));
    }
    TestInputContext chrome(manager, "google-chrome");
    assert(detector.inChromiumAddressBar(chrome));
    TestInputContext editor(manager, "gnome-text-editor");
    assert(!detector.inChromiumAddressBar(editor));
    areca::WindowFocusTrackerTestAccess::seedFocus(tracker, ":1.54", "/omnibox",
                                                   "chrome", false);
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    assert(detector.inChromiumAddressBar(chrome));
    assert(detector.inChromiumAddressBar(edge));

    areca::WindowFocusTrackerTestAccess::loseFocus(tracker, ":1.54",
                                                   "/omnibox");
    assert(!detector.inChromiumAddressBar(chrome));
    assert(tracker.browserFocus().className.empty());
    areca::WindowFocusTrackerTestAccess::seedFocus(tracker, ":1.54", "/omnibox",
                                                   "chrome", false);
    areca::WindowFocusTrackerTestAccess::setClass(tracker, "OmniboxViewViews");
    areca::WindowFocusTrackerTestAccess::connectionLost(tracker);
    assert(!detector.inChromiumAddressBar(chrome));
    assert(tracker.browserFocus().className.empty());
  }

  // Case 5: Tracker focus must not leak to another application.
  {
    WindowFocusTracker tracker;
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.20", "/terminal", "ghostty", true);
    InputTypeDetector detector(&tracker);

    assert(detector.isTerminal(""));
    assert(!detector.isTerminal("firefox"));

    // Different VS Code executable and desktop IDs still refer to the same
    // app whose embedded terminal owns the AT-SPI focus.
    areca::WindowFocusTrackerTestAccess::seedFocus(tracker, ":1.20",
                                                   "/terminal", "code", true);
    assert(detector.isTerminal("visual-studio-code"));

    // A delayed blur for the old node must not clear a newer focus.
    areca::WindowFocusTrackerTestAccess::seedFocus(
        tracker, ":1.21", "/new-entry", "firefox", false);
    areca::WindowFocusTrackerTestAccess::loseFocus(tracker, ":1.20",
                                                   "/terminal");
    assert(tracker.focusProgram() == "firefox");

    // Losing the active focus clears all public focus identity and flags.
    areca::WindowFocusTrackerTestAccess::loseFocus(tracker, ":1.21",
                                                   "/new-entry");
    assert(tracker.focusProgram().empty());
    assert(!tracker.isTerminalFocused());

    // A recovered AT-SPI connection makes the tracker usable again.
    areca::WindowFocusTrackerTestAccess::connectionLost(tracker);
    assert(!tracker.isValid());
    areca::WindowFocusTrackerTestAccess::connectionEstablished(tracker);
    assert(tracker.isValid());
  }

  // Case 5: canForwardBackspaces tests
  {
    areca::im::InputContextManager manager;
    InputTypeDetector detector(nullptr);

    // Wayland and XIM always forward
    {
      TestInputContext ic(manager, "app");
      assert(detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "app");
      assert(detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "app");
      assert(detector.canForwardBackspaces(ic));
    }

    // SDL clients never forward backspaces
    {
      TestInputContext ic(manager, "SDL_Game");
      ic.setCapabilityFlags(areca::im::CapabilityFlag::KeyEventOrderFix);
      assert(!detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "sdl2-game");
      assert(!detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "/usr/bin/SDL_Game");
      assert(!detector.canForwardBackspaces(ic));
    }
    {
      TestInputContext ic(manager, "SDL3_App.desktop");
      assert(!detector.canForwardBackspaces(ic));
    }

    // Ứng dụng thường nhận được Backspace mà IBus forward.
    {
      TestInputContext ic(manager, "app");
      assert(detector.canForwardBackspaces(ic));
    }
  }

  return 0;
}
