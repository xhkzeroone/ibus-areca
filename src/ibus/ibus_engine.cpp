#include "ibus/ibus_engine.h"

#include <exception>
#include <string>

#include "areca_engine.h"
#include "ibus/ibus_host.h"
#include "ibus/ibus_input_context.h"

struct ArecaIbusEngine {
  IBusEngine parent;
  areca::IbusInputContext *context;
};

struct ArecaIbusEngineClass {
  IBusEngineClass parent;
};

G_DEFINE_TYPE(ArecaIbusEngine, areca_ibus_engine, IBUS_TYPE_ENGINE)

namespace {

areca::IbusInputContext *contextOf(IBusEngine *engine) {
  return reinterpret_cast<ArecaIbusEngine *>(engine)->context;
}

areca::ArecaEngine &core() { return areca::IbusHost::instance().engine(); }

gboolean processKeyEvent(IBusEngine *engine, guint keyval, guint keycode,
                         guint state) {
  auto *context = contextOf(engine);
  if (!context) {
    return FALSE;
  }
  try {
    return context->processKeyEvent(keyval, keycode, state);
  } catch (const std::exception &error) {
    g_warning("areca: key event failed: %s", error.what());
    return FALSE;
  }
}

void focusIn(IBusEngine *engine) {
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)->focus_in(engine);
  auto *context = contextOf(engine);
  if (!context) {
    return;
  }
  context->focusIn();
  areca::IbusHost::instance().setFocusedContext(context);
  // IBus chỉ bắt đầu gửi surrounding text sau khi engine hỏi ít nhất một lần.
  if (engine->client_capabilities & IBUS_CAP_SURROUNDING_TEXT) {
    IBusText *text = nullptr;
    guint cursor = 0;
    guint anchor = 0;
    ibus_engine_get_surrounding_text(engine, &text, &cursor, &anchor);
  }
  core().activate(*context);
  context->refreshStatusArea();
}

#if IBUS_CHECK_VERSION(1, 5, 27)
void focusInId(IBusEngine *engine, const gchar *objectPath,
               const gchar *client) {
  if (auto *context = contextOf(engine); context && client) {
    context->setClient(client);
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)
      ->focus_in_id(engine, objectPath, client);
}
#endif

void focusOut(IBusEngine *engine) {
  if (auto *context = contextOf(engine)) {
    core().deactivate(*context);
    context->focusOut();
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)->focus_out(engine);
}

void reset(IBusEngine *engine) {
  if (auto *context = contextOf(engine)) {
    core().reset(*context);
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)->reset(engine);
}

void enable(IBusEngine *engine) {
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)->enable(engine);
  core().announceSwitchedIn();
}

void disable(IBusEngine *engine) {
  if (auto *context = contextOf(engine)) {
    core().deactivate(*context);
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)->disable(engine);
}

void setCapabilities(IBusEngine *engine, guint caps) {
  if (auto *context = contextOf(engine)) {
    auto flags = context->capabilityFlags();
    flags.set(areca::im::CapabilityFlag::Preedit, caps & IBUS_CAP_PREEDIT_TEXT);
    flags.set(areca::im::CapabilityFlag::SurroundingText,
              caps & IBUS_CAP_SURROUNDING_TEXT);
    context->setCapabilityFlags(flags);
    if (!(caps & IBUS_CAP_SURROUNDING_TEXT)) {
      context->surroundingText().invalidate();
    }
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)
      ->set_capabilities(engine, caps);
}

void setContentType(IBusEngine *engine, guint purpose, guint hints) {
  if (auto *context = contextOf(engine)) {
    auto flags = context->capabilityFlags();
    flags.set(areca::im::CapabilityFlag::Password,
              purpose == IBUS_INPUT_PURPOSE_PASSWORD ||
                  purpose == IBUS_INPUT_PURPOSE_PIN);
    flags.set(areca::im::CapabilityFlag::Url,
              purpose == IBUS_INPUT_PURPOSE_URL);
#if IBUS_CHECK_VERSION(1, 5, 24)
    flags.set(areca::im::CapabilityFlag::Terminal,
              purpose == IBUS_INPUT_PURPOSE_TERMINAL);
#endif
    context->setCapabilityFlags(flags);
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)
      ->set_content_type(engine, purpose, hints);
}

void setSurroundingText(IBusEngine *engine, IBusText *text, guint cursor,
                        guint anchor) {
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)
      ->set_surrounding_text(engine, text, cursor, anchor);
  auto *context = contextOf(engine);
  if (!context) {
    return;
  }
  const char *value = text ? ibus_text_get_text(text) : nullptr;
  context->surroundingText().setText(value ? value : "", cursor, anchor);
  core().surroundingTextUpdated(*context);
}

void propertyActivate(IBusEngine *engine, const gchar *name, guint state) {
  if (auto *context = contextOf(engine); context && name) {
    context->activateProperty(name, state);
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)
      ->property_activate(engine, name, state);
}

void candidateClicked(IBusEngine *engine, guint index, guint button,
                      guint state) {
  if (auto *context = contextOf(engine)) {
    context->selectCandidate(index);
  }
  IBUS_ENGINE_CLASS(areca_ibus_engine_parent_class)
      ->candidate_clicked(engine, index, button, state);
}

void destroy(IBusObject *object) {
  auto *self = reinterpret_cast<ArecaIbusEngine *>(object);
  // Xoá context trước để timer và scheduler không gọi vào engine đã huỷ.
  delete self->context;
  self->context = nullptr;
  IBUS_OBJECT_CLASS(areca_ibus_engine_parent_class)->destroy(object);
}

} // namespace

static void areca_ibus_engine_init(ArecaIbusEngine *self) {
  self->context = new areca::IbusInputContext(IBUS_ENGINE(self));
}

static void areca_ibus_engine_class_init(ArecaIbusEngineClass *klass) {
  IBUS_OBJECT_CLASS(klass)->destroy = destroy;
  auto *engineClass = IBUS_ENGINE_CLASS(klass);
  engineClass->process_key_event = processKeyEvent;
  engineClass->focus_in = focusIn;
#if IBUS_CHECK_VERSION(1, 5, 27)
  engineClass->focus_in_id = focusInId;
#endif
  engineClass->focus_out = focusOut;
  engineClass->reset = reset;
  engineClass->enable = enable;
  engineClass->disable = disable;
  engineClass->set_capabilities = setCapabilities;
  engineClass->set_content_type = setContentType;
  engineClass->set_surrounding_text = setSurroundingText;
  engineClass->property_activate = propertyActivate;
  engineClass->candidate_clicked = candidateClicked;
}
