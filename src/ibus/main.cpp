#include <ibus.h>

#include "ibus/ibus_engine.h"
#include "ibus/ibus_host.h"

namespace {

constexpr const char *kEngineName = "areca";
constexpr const char *kBusName = "org.freedesktop.IBus.Areca";

// Chạy tay khi phát triển: tự đăng ký component với ibus-daemon đang chạy, khỏi
// phải cài file XML vào thư mục hệ thống.
void registerComponent(IBusBus *bus) {
  IBusComponent *component = ibus_component_new(
      kBusName, "Areca Vietnamese input method", ARECA_VERSION, "MIT", "Areca",
      "", ARECA_EXEC_PATH " --ibus", "ibus-areca");
  ibus_component_add_engine(
      component,
      ibus_engine_desc_new(kEngineName, "Areca", "Bộ gõ tiếng Việt Areca", "vi",
                           "MIT", "Areca", "ibus-areca", "default"));
  ibus_bus_register_component(bus, component);
  g_object_unref(component);
}

void onDisconnected(IBusBus *, gpointer) { ibus_quit(); }

} // namespace

int main(int argc, char **argv) {
  gboolean ibusMode = FALSE;
  GOptionEntry entries[] = {
      {"ibus", 'i', 0, G_OPTION_ARG_NONE, &ibusMode,
       "Được ibus-daemon khởi chạy", nullptr},
      {nullptr, 0, 0, G_OPTION_ARG_NONE, nullptr, nullptr, nullptr},
  };
  GError *error = nullptr;
  GOptionContext *options = g_option_context_new("- engine IBus Areca");
  g_option_context_add_main_entries(options, entries, nullptr);
  if (!g_option_context_parse(options, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    g_error_free(error);
    g_option_context_free(options);
    return 1;
  }
  g_option_context_free(options);

  ibus_init();
  IBusBus *bus = ibus_bus_new();
  if (!ibus_bus_is_connected(bus)) {
    g_printerr("ibus-areca: cannot connect to ibus-daemon\n");
    return 1;
  }
  g_signal_connect(bus, "disconnected", G_CALLBACK(onDisconnected), nullptr);

  // Khởi tạo lõi trước khi nhận engine đầu tiên để cấu hình, AT-SPI và thiết
  // bị phát phím sẵn sàng.
  areca::IbusHost::instance();

  IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(bus));
  ibus_factory_add_engine(factory, kEngineName, areca_ibus_engine_get_type());

  if (ibusMode) {
    ibus_bus_request_name(bus, kBusName, 0);
  } else {
    registerComponent(bus);
  }

  ibus_main();
  g_object_unref(factory);
  g_object_unref(bus);
  return 0;
}
