#include "x11_focus_probe.h"

#include "atspi/atspi_accessible.h"

#ifdef ARECA_HAS_XCB
#include <xcb/xcb.h>
#endif

namespace areca {

#ifdef ARECA_HAS_XCB

struct X11FocusProbe::Impl {
  xcb_connection_t *connection = nullptr;
  xcb_atom_t pidAtom = XCB_ATOM_NONE;
  xcb_window_t lastFocus = XCB_WINDOW_NONE;
  std::string lastProgram;
  int connectCountdown = 0;

  ~Impl() { disconnect(); }

  void disconnect() {
    if (connection) {
      xcb_disconnect(connection);
      connection = nullptr;
    }
    pidAtom = XCB_ATOM_NONE;
    lastFocus = XCB_WINDOW_NONE;
    lastProgram.clear();
  }

  bool ensureConnected() {
    if (connection && xcb_connection_has_error(connection)) {
      disconnect();
    }
    if (connection) {
      return true;
    }
    // Không có X server thì thử lại thưa để khỏi tốn mỗi vòng 100 ms.
    if (connectCountdown > 0) {
      --connectCountdown;
      return false;
    }
    connection = xcb_connect(nullptr, nullptr);
    if (!connection || xcb_connection_has_error(connection)) {
      disconnect();
      connectCountdown = 50;
      return false;
    }
    const char name[] = "_NET_WM_PID";
    auto *reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, 1, sizeof(name) - 1, name),
        nullptr);
    if (reply) {
      pidAtom = reply->atom;
      free(reply);
    }
    if (pidAtom == XCB_ATOM_NONE) {
      disconnect();
      connectCountdown = 50;
      return false;
    }
    return true;
  }

  // Cửa sổ nhận focus thường là cửa sổ con, _NET_WM_PID nằm ở cửa sổ gốc của
  // ứng dụng nên đi ngược lên cha tới khi gặp.
  int pidOfWindow(xcb_window_t window) {
    for (int depth = 0; depth < 8 && window != XCB_WINDOW_NONE; ++depth) {
      auto *property = xcb_get_property_reply(
          connection,
          xcb_get_property(connection, 0, window, pidAtom, XCB_ATOM_CARDINAL, 0,
                           1),
          nullptr);
      if (property) {
        int pid = 0;
        if (property->value_len == 1 && property->format == 32) {
          pid = static_cast<int>(
              *static_cast<uint32_t *>(xcb_get_property_value(property)));
        }
        free(property);
        if (pid > 0) {
          return pid;
        }
      }
      auto *tree = xcb_query_tree_reply(
          connection, xcb_query_tree(connection, window), nullptr);
      if (!tree) {
        return 0;
      }
      const xcb_window_t parent = tree->parent;
      const xcb_window_t root = tree->root;
      free(tree);
      if (parent == root) {
        return 0;
      }
      window = parent;
    }
    return 0;
  }

  std::string focusedProgram() {
    if (!ensureConnected()) {
      return {};
    }
    auto *focus = xcb_get_input_focus_reply(
        connection, xcb_get_input_focus(connection), nullptr);
    if (!focus) {
      disconnect();
      return {};
    }
    const xcb_window_t window = focus->focus;
    free(focus);
    // None và PointerRoot nghĩa là cửa sổ Wayland gốc đang giữ focus.
    if (window <= 1) {
      lastFocus = XCB_WINDOW_NONE;
      lastProgram.clear();
      return {};
    }
    if (window == lastFocus) {
      return lastProgram;
    }
    lastFocus = window;
    const std::string program =
        AtspiAccessible::programForProcess(pidOfWindow(window));
    lastProgram = program;
    return lastProgram;
  }
};

#else

struct X11FocusProbe::Impl {
  std::string focusedProgram() { return {}; }
};

#endif

X11FocusProbe::X11FocusProbe() : impl_(std::make_unique<Impl>()) {}
X11FocusProbe::~X11FocusProbe() = default;

std::string X11FocusProbe::focusedProgram() { return impl_->focusedProgram(); }

} // namespace areca
