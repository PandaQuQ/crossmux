#include "FanqieLog.h"

#if !FANQIE_NOVEL

extern "C" void fanqieNoteRestart(const char* why) { (void)why; }

namespace fanqie_internal {
constexpr int kLogOmitted = 0;
}

#else

#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>
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
constexpr uint32_t kMagic = 0xF401BEEFu;
constexpr int kSlots = 12;
constexpr int kText = 48;

struct Trail {
  uint32_t magic;
  uint32_t count;
  char text[kSlots][kText];
};

// Survives esp_restart / ESP.restart. A power loss leaves this as garbage.
RTC_NOINIT_ATTR Trail trail;

bool valid() { return trail.magic == kMagic && trail.count < 100000u; }

void ensure() {
  if (valid()) return;
  memset(&trail, 0, sizeof(trail));
  trail.magic = kMagic;
}

void scrub(char* text) {
  static constexpr const char* kKeys[] = {"cookie", "token", "sessionid", "csrf", "qrcode"};
  for (char* cursor = text; *cursor; ++cursor) {
    for (const char* key : kKeys) {
      const char* k = key;
      const char* t = cursor;
      while (*k && tolower(static_cast<unsigned char>(*t)) == *k) {
        ++t;
        ++k;
      }
      if (*k) continue;
      char* value = cursor + (t - cursor);
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

bool appendLine(const char* text) {
  // SdFat mkdir returns false when the directory already exists. The first boot
  // created /.crosspoint/fanqie, so every later boot was skipping the append
  // and leaving the original 22-byte line in place.
  if (!Storage.ready()) return false;
  if (!Storage.exists("/.crosspoint") && !Storage.mkdir("/.crosspoint")) return false;
  if (!Storage.exists(kDir) && !Storage.mkdir(kDir)) return false;
  HalFile existing = Storage.open(kLog, O_RDONLY);
  size_t size = 0;
  if (existing) {
    size = existing.size();
    existing.close();
    existing = HalFile{};
  }
  if (size >= kMaxBytes) {
    Storage.remove(kPrevious);
    if (!Storage.rename(kLog, kPrevious)) Storage.remove(kLog);
  }
  // O_AT_END seeks to the current length. It does not truncate.
  HalFile file = Storage.open(kLog, O_RDWR | O_CREAT | O_AT_END);
  if (!file) return false;
  const size_t length = strlen(text);
  const bool wrote = file.write(text, length) == length && file.write("\n", 1) == 1;
  file.flush();
  file.close();
  file = HalFile{};
  if (!wrote) LOG_ERR("FQ", "log append failed");
  return wrote;
}

}  // namespace

void setShow(void (*fn)(const char* text)) { (void)fn; }

void store(const char* text, char* out, const size_t outCap) {
  ensure();
  snprintf(out, outCap, "%s", text);
  scrub(out);
  const uint32_t slot = trail.count % kSlots;
  snprintf(trail.text[slot], kText, "%s", out);
  trail.count++;
  LOG_INF("FQ", "%s", out);
}

void line(const char* text) {
  if (!text || !text[0]) return;
  char buf[kText];
  // RTC and the serial log only. Painting is the activity's job: doing it from
  // every step refreshed the panel again while the Wi-Fi task was already up.
  store(text, buf, sizeof(buf));
}

void persist(const char* text) {
  if (!text || !text[0]) return;
  char buf[kText];
  store(text, buf, sizeof(buf));
  if (!appendLine(buf)) LOG_ERR("FQ", "crumb not on card");
}

void heapProbe(const char* tag) {
  if (!tag || !tag[0]) tag = "heap";
  char began[64];
  snprintf(began, sizeof(began), "%s begin", tag);
  // Flushed before the walk so a crash inside the checker still leaves a line.
  if (!appendLine(began)) LOG_ERR("FQ", "heap probe not on card");
  const bool ok = heap_caps_check_integrity_all(true);
  constexpr uint32_t kInternal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  constexpr uint32_t kDma = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
  constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM;
  char row[180];
  snprintf(row, sizeof(row), "%s ok=%d int=%u/%u dma=%u/%u psram=%u/%u", tag, ok ? 1 : 0,
           static_cast<unsigned>(heap_caps_get_free_size(kInternal)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(kInternal)),
           static_cast<unsigned>(heap_caps_get_free_size(kDma)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(kDma)),
           static_cast<unsigned>(heap_caps_get_free_size(kPsram)),
           static_cast<unsigned>(heap_caps_get_largest_free_block(kPsram)));
  LOG_INF("FQ", "%s", row);
  char crumb[kText];
  store(tag, crumb, sizeof(crumb));
  if (!appendLine(row)) LOG_ERR("FQ", "heap probe not on card");
}

extern "C" void fanqieHeapProbe(const char* tag) { heapProbe(tag); }

void noteBoot() {
  const bool keep = valid();
  const uint32_t count = keep ? trail.count : 0;
  char boot[96];
  snprintf(boot, sizeof(boot), "boot reset=%s crumbs=%u", resetName(esp_reset_reason()), static_cast<unsigned>(count));
  appendLine(boot);
  if (keep && count > 0) {
    const uint32_t start = count > kSlots ? count - kSlots : 0;
    for (uint32_t i = start; i < count; ++i) {
      char row[64];
      snprintf(row, sizeof(row), "crumb %u %s", static_cast<unsigned>(i + 1), trail.text[i % kSlots]);
      appendLine(row);
    }
  }
  memset(&trail, 0, sizeof(trail));
  trail.magic = kMagic;
}

}  // namespace FanqieLog

extern "C" void fanqieNoteRestart(const char* why) {
  char buf[48];
  FanqieLog::store(why ? why : "restart", buf, sizeof(buf));
}

#endif
