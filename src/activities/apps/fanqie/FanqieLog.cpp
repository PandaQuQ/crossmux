#include "FanqieLog.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kLogOmitted = 0;
}

#else

#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace FanqieLog {
namespace {

constexpr char kDir[] = "/.crosspoint/fanqie";
constexpr char kLog[] = "/.crosspoint/fanqie/fanqie.log";
constexpr char kPrevious[] = "/.crosspoint/fanqie/fanqie.log.1";
constexpr size_t kMaxBytes = 64 * 1024;

bool keyAt(const char* text, const char* key) {
  for (; *key; ++text, ++key) {
    if (tolower(static_cast<unsigned char>(*text)) != *key) return false;
  }
  return true;
}

// Blank the value after cookie / token / session / csrf / qrcode so a later
// caller cannot append a secret by accident. The QR image and session id
// never belong in this file.
void scrub(char* text) {
  static constexpr const char* kKeys[] = {"cookie", "token", "sessionid", "csrf", "qrcode"};
  for (char* cursor = text; *cursor; ++cursor) {
    for (const char* key : kKeys) {
      if (!keyAt(cursor, key)) continue;
      char* value = cursor + strlen(key);
      while (*value && *value != '=' && *value != ':') ++value;
      if (!*value) continue;
      ++value;
      while (*value == ' ' || *value == '"') ++value;
      while (*value && *value != ' ' && *value != ',' && *value != '&' && *value != '"' && *value != '\n') {
        *value++ = '*';
      }
    }
  }
}

void rotateIfNeeded() {
  HalFile existing = Storage.open(kLog, O_RDONLY);
  if (!existing) return;
  const size_t size = existing.size();
  existing = HalFile{};
  if (size < kMaxBytes) return;
  Storage.remove(kPrevious);
  if (!Storage.rename(kLog, kPrevious)) Storage.remove(kLog);
}

}  // namespace

void line(const char* text) {
  if (!text || !text[0]) return;
  char buf[180];
  snprintf(buf, sizeof(buf), "%s", text);
  scrub(buf);
  LOG_INF("FQ", "%s", buf);
  if (!Storage.mkdir(kDir)) return;
  rotateIfNeeded();
  // O_AT_END opens at the end. openFileForWrite would truncate the log.
  HalFile file = Storage.open(kLog, O_RDWR | O_CREAT | O_AT_END);
  if (!file) return;
  file.write(buf, strlen(buf));
  file.write("\n", 1);
}

}  // namespace FanqieLog

#endif
