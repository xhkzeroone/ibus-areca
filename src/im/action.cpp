#include "action.h"

#include <glib.h>

namespace areca::im {

void startProcess(const std::vector<std::string> &args) {
  if (args.empty()) {
    return;
  }
  std::vector<gchar *> argv;
  argv.reserve(args.size() + 1);
  for (const auto &arg : args) {
    argv.push_back(const_cast<gchar *>(arg.c_str()));
  }
  argv.push_back(nullptr);
  GError *error = nullptr;
  if (!g_spawn_async(nullptr, argv.data(), nullptr, G_SPAWN_SEARCH_PATH,
                     nullptr, nullptr, nullptr, &error)) {
    g_warning("areca: cannot start %s: %s", args.front().c_str(),
              error->message);
    g_error_free(error);
  }
}

} // namespace areca::im
