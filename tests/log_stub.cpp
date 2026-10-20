// log.h for the tests: every line to stdout.
#include <cstdarg>
#include <cstdio>

#include "log.h"

namespace re4cc {
void log_init(const char*) {}
void log_shutdown() {}
void logf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
  std::printf("\n");
}
}  // namespace re4cc
