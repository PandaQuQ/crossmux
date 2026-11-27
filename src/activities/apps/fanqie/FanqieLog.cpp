#include "FanqieLog.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kLogOmitted = 0;
}

#else

#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

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

bool commit(HalFile& file, const char* text) {
  const size_t length = strlen(text);
  if (file.write(text, length) != length || file.write("\n", 1) != 1) return false;
  file.flush();
  file.close();
  return true;
}

void rotateIfNeeded() {
  HalFile existing = Storage.open(kLog, O_RDONLY);
  if (!existing) return;
  const size_t size = existing.size();
  existing.flush();
  existing.close();
  existing = HalFile{};
  if (size < kMaxBytes) return;
  Storage.remove(kPrevious);
  if (!Storage.rename(kLog, kPrevious)) Storage.remove(kLog);
}

const char* resetName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "UNKNOWN";
  }
}

void lastLine(char* out, const size_t cap) {
  if (!out || cap < 2) return;
  out[0] = '\0';
  HalFile file = Storage.open(kLog, O_RDONLY);
  if (!file) return;
  const size_t size = file.size();
  if (size == 0) {
    file.close();
    return;
  }
  const size_t window = size < 160 ? size : 160;
  if (!file.seek(size - window)) {
    file.close();
    return;
  }
  char raw[160];
  const int got = file.read(raw, window);
  file.close();
  if (got <= 0) return;
  int end = got;
  while (end > 0 && (raw[end - 1] == '\n' || raw[end - 1] == '\r')) --end;
  int start = end;
  while (start > 0 && raw[start - 1] != '\n') --start;
  size_t used = 0;
  for (int i = start; i < end && used + 1 < cap; ++i) {
    const char c = raw[i];
    out[used++] = (c >= 32 && c != '"') ? c : ' ';
  }
  out[used] = '\0';
}

}  // namespace

void line(const char* text) {
  if (!text || !text[0]) return;
  char buf[240];
  snprintf(buf, sizeof(buf), "%s", text);
  scrub(buf);
  LOG_INF("FQ", "%s", buf);
  if (!Storage.ready()) {
    LOG_ERR("FQ", "log skipped: SD not mounted");
    return;
  }
  if (!Storage.mkdir(kDir) || !Storage.exists(kDir)) {
    LOG_ERR("FQ", "log skipped: %s missing", kDir);
    return;
  }
  rotateIfNeeded();
  HalFile file = Storage.open(kLog, O_RDWR | O_CREAT | O_AT_END);
  if (!file) {
    LOG_ERR("FQ", "log open failed");
    return;
  }
  if (!commit(file, buf)) LOG_ERR("FQ", "log flush failed");
  file = HalFile{};
}

void noteBoot() {
  char previous[96];
  lastLine(previous, sizeof(previous));
  char buf[240];
  snprintf(buf, sizeof(buf), "boot reset=%s after=%s", resetName(esp_reset_reason()), previous[0] ? previous : "-");
  line(buf);
}

}  // namespace FanqieLog

#endif
