#include "ibus/ibus_input_context.h"

#include "areca_engine.h"
#include "ibus/ibus_host.h"

namespace areca {
namespace {

constexpr uint64_t kInformationDurationUsec = 1500 * 1000;

// Keycode evdev cho các phím Areca có thể forward.
guint evdevKeycode(im::KeySym sym) {
  switch (sym) {
  case im::Key_BackSpace:
    return 14;
  case im::Key_Left:
    return 105;
  case im::Key_Shift_L:
    return 42;
  default:
    return 0;
  }
}

IBusText *newText(const std::string &text) {
  return ibus_text_new_from_string(text.c_str());
}

IBusProperty *newProperty(im::SimpleAction *action, IBusPropType type) {
  const auto &label = action->longText().empty() || type != PROP_TYPE_MENU
                          ? action->shortText()
                          : action->shortText() + ": " + action->longText();
  return ibus_property_new(
      action->name().c_str(), type, newText(label), action->icon().c_str(),
      newText(action->longText().empty() ? action->shortText()
                                         : action->longText()),
      TRUE, TRUE,
      action->isChecked() ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED, nullptr);
}

} // namespace

IbusInputContext::IbusInputContext(IBusEngine *engine) : engine_(engine) {
  IbusHost::instance().addContext(this);
}

IbusInputContext::~IbusInputContext() {
  IbusHost::instance().removeContext(this);
}

void IbusInputContext::setClient(const std::string &client) {
  const auto colon = client.find(':');
  if (colon != std::string::npos) {
    setProgram(client.substr(colon + 1));
  }
}

bool IbusInputContext::processKeyEvent(guint keyval, guint keycode,
                                       guint state) {
  const bool release = state & IBUS_RELEASE_MASK;
  auto states = im::KeyStates::fromInteger(
      state & ~static_cast<guint>(IBUS_RELEASE_MASK | IBUS_FORWARD_MASK |
                                  IBUS_IGNORED_MASK | IBUS_HANDLED_MASK));
  if (release) {
    if (heldKeyval_ == keyval) {
      heldKeyval_ = 0;
    }
  } else {
    if (heldKeyval_ == keyval) {
      states.set(im::KeyState::Repeat);
    }
    heldKeyval_ = keyval;
  }
  // Keycode của Areca theo X11, bằng keycode evdev cộng 8.
  const im::Key key(static_cast<im::KeySym>(keyval), states,
                    static_cast<int>(keycode) + 8);
  im::KeyEvent event(this, key, release);
  IbusHost::instance().engine().keyEvent(event);
  return event.filtered();
}

void IbusInputContext::commitStringImpl(const std::string &text) {
  ibus_engine_commit_text(engine_, newText(text));
}

void IbusInputContext::deleteSurroundingTextImpl(int offset,
                                                 unsigned int size) {
  ibus_engine_delete_surrounding_text(engine_, offset, size);
}

void IbusInputContext::forwardKeyImpl(const im::ForwardKeyEvent &event) {
  const auto &key = event.rawKey();
  guint state = key.states().toInteger();
  if (event.isRelease()) {
    state |= IBUS_RELEASE_MASK;
  }
  ibus_engine_forward_key_event(engine_, key.sym(), evdevKeycode(key.sym()),
                                state);
}

void IbusInputContext::updatePreeditImpl() {
  const auto &panel = inputPanel();
  const auto &text =
      panel.clientPreedit().empty() ? panel.preedit() : panel.clientPreedit();
  if (text.empty()) {
    ibus_engine_hide_preedit_text(engine_);
    return;
  }
  const int cursor =
      text.cursor() >= 0 ? text.cursor() : static_cast<int>(text.textLength());
  IBusText *preedit = newText(text.toString());
  ibus_text_append_attribute(preedit, IBUS_ATTR_TYPE_UNDERLINE,
                             IBUS_ATTR_UNDERLINE_SINGLE, 0,
                             static_cast<gint>(text.textLength()));
  // Mất focus thì client commit nốt chữ đang soạn thay vì xoá mất.
  ibus_engine_update_preedit_text_with_mode(engine_, preedit,
                                            static_cast<guint>(cursor), TRUE,
                                            IBUS_ENGINE_PREEDIT_COMMIT);
}

void IbusInputContext::updateUserInterfaceImpl(
    im::UserInterfaceComponent component, bool) {
  if (component != im::UserInterfaceComponent::InputPanel) {
    return;
  }
  updatePreeditImpl();
  updateLookupTable();
}

void IbusInputContext::updateLookupTable() {
  auto &panel = inputPanel();
  const auto list = panel.candidateList();
  if (!list || list->size() == 0) {
    ibus_engine_hide_lookup_table(engine_);
    if (!showingInformation_) {
      ibus_engine_hide_auxiliary_text(engine_);
    }
    return;
  }
  showingInformation_ = false;
  informationTimer_.reset();
  IBusLookupTable *table = ibus_lookup_table_new(
      static_cast<guint>(list->pageSize()),
      static_cast<guint>(std::max(list->cursorIndex(), 0)), TRUE, TRUE);
  ibus_lookup_table_set_orientation(
      table, list->layoutHint() == im::CandidateLayoutHint::Horizontal
                 ? IBUS_ORIENTATION_HORIZONTAL
                 : IBUS_ORIENTATION_VERTICAL);
  const auto &labels = list->labels();
  for (int i = 0; i < list->size(); ++i) {
    ibus_lookup_table_append_candidate(
        table, newText(list->candidate(i).text().toString()));
    if (static_cast<size_t>(i) < labels.size()) {
      ibus_lookup_table_set_label(table, static_cast<guint>(i),
                                  newText(labels[i]));
    }
  }
  ibus_engine_update_lookup_table(engine_, table, TRUE);
  if (!panel.auxUp().empty()) {
    ibus_engine_update_auxiliary_text(engine_,
                                      newText(panel.auxUp().toString()), TRUE);
  } else {
    ibus_engine_hide_auxiliary_text(engine_);
  }
}

void IbusInputContext::selectCandidate(guint index) {
  const auto list = inputPanel().candidateList();
  if (!list || static_cast<int>(index) >= list->size()) {
    return;
  }
  list->candidate(static_cast<int>(index)).select(this);
}

void IbusInputContext::showInformation(const std::string &text) {
  if (inputPanel().candidateList()) {
    return;
  }
  showingInformation_ = true;
  ibus_engine_update_auxiliary_text(engine_, newText(text), TRUE);
  informationTimer_ = IbusHost::instance().eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, im::now(CLOCK_MONOTONIC) + kInformationDurationUsec, 0,
      [this](im::EventSourceTime *, uint64_t) {
        auto timer = std::move(informationTimer_);
        showingInformation_ = false;
        if (!inputPanel().candidateList()) {
          ibus_engine_hide_auxiliary_text(engine_);
        }
        return false;
      });
}

void IbusInputContext::refreshStatusArea() {
  IBusPropList *props = ibus_prop_list_new();
  // Panel IBus vẽ biểu tượng khay theo property InputMode, nên TrayIcon trong
  // cấu hình đổi được biểu tượng như trên fcitx5.
  auto &engine = IbusHost::instance().engine();
  IBusProperty *inputMode = ibus_property_new(
      "InputMode", PROP_TYPE_NORMAL, newText(engine.subModeLabel(*this)),
      engine.overrideIcon().c_str(), newText(engine.subModeLabel(*this)), TRUE,
      TRUE, PROP_STATE_UNCHECKED, nullptr);
  ibus_property_set_symbol(inputMode, newText("Ă"));
  ibus_prop_list_append(props, inputMode);
  for (auto *action : statusArea().actions()) {
    if (auto *menu = action->menu()) {
      IBusPropList *items = ibus_prop_list_new();
      for (auto *item : menu->actions()) {
        ibus_prop_list_append(items, newProperty(item, PROP_TYPE_RADIO));
      }
      IBusProperty *property = newProperty(action, PROP_TYPE_MENU);
      ibus_property_set_sub_props(property, items);
      ibus_prop_list_append(props, property);
    } else {
      ibus_prop_list_append(props, newProperty(action, action->isCheckable()
                                                           ? PROP_TYPE_TOGGLE
                                                           : PROP_TYPE_NORMAL));
    }
  }
  ibus_engine_register_properties(engine_, props);
}

void IbusInputContext::activateProperty(const std::string &name, guint state) {
  auto &engine = IbusHost::instance().engine();
  auto *action = engine.userInterfaceManager().lookupAction(name);
  if (!action) {
    return;
  }
  bool isMenuItem = false;
  for (auto *top : statusArea().actions()) {
    if (top->menu()) {
      for (auto *item : top->menu()->actions()) {
        isMenuItem = isMenuItem || item == action;
      }
    }
  }
  // Panel báo cả mục radio bị bỏ chọn; chỉ mục được chọn mới đổi cấu hình.
  if (isMenuItem && state != PROP_STATE_CHECKED) {
    return;
  }
  action->activate(this);
}

} // namespace areca
