#include "log_writer.h"

#include <glib.h>
#include <unistd.h>

namespace areca::im {
namespace {

GLogWriterOutput writeLog(GLogLevelFlags level, const GLogField *fields,
                          gsize fieldCount, gpointer userData) {
  if (isatty(STDERR_FILENO) || g_log_writer_is_journald(STDERR_FILENO)) {
    return g_log_writer_default(level, fields, fieldCount, userData);
  }
  if (g_log_writer_journald(level, fields, fieldCount, userData) ==
      G_LOG_WRITER_HANDLED) {
    return G_LOG_WRITER_HANDLED;
  }
  return g_log_writer_default(level, fields, fieldCount, userData);
}

} // namespace

void installLogWriter() { g_log_set_writer_func(writeLog, nullptr, nullptr); }

} // namespace areca::im
