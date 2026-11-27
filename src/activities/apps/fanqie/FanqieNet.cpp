#include "FanqieNet.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kNetOmitted = 0;
}

#else

#include <Arduino.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WeReadHttpClient.h>
#include <WiFi.h>
#include <freertos/task.h>
#include <strings.h>

#include <cstring>
#include <string>

#include "FanqieLog.h"

namespace FanqieNet {
namespace {

constexpr size_t kMaxBody = 256 * 1024;
// Same size as WeReadClient::Operation::kIoBufferSize. It lives for the process
// so the handshake does not allocate its read buffer.
uint8_t ioBuffer[4096];

struct WriteState {
  HalFile* file = nullptr;
  size_t written = 0;
  bool overflow = false;
};

bool onData(const uint8_t* data, size_t len, WriteState* state) {
  if (!state || !state->file || state->overflow) return false;
  if (state->written + len > kMaxBody) {
    state->overflow = true;
    return false;
  }
  if (state->file->write(data, len) != len) return false;
  state->written += len;
  return true;
}

void note(const char* text) { FanqieLog::line(text); }

void heapLine(char* out, size_t cap) {
  snprintf(out, cap, "free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
           static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

bool copyHost(const char* url, char* host, size_t cap) {
  const char* start = strstr(url, "://");
  if (!start) return false;
  start += 3;
  const char* end = strchr(start, '/');
  if (!end) end = start + strlen(start);
  for (const char* cursor = start; cursor < end; ++cursor) {
    if (*cursor == ':') {
      end = cursor;
      break;
    }
  }
  const size_t length = static_cast<size_t>(end - start);
  if (length == 0 || length + 1 > cap) return false;
  memcpy(host, start, length);
  host[length] = '\0';
  return true;
}

void copyPath(const char* url, char* out, size_t cap) {
  const char* start = strstr(url, "://");
  const char* path = start ? strchr(start + 3, '/') : url;
  if (!path) path = "/";
  size_t used = 0;
  for (; path[used] && path[used] != '?' && used + 1 < cap; ++used) out[used] = path[used];
  out[used] = '\0';
}

}  // namespace

bool connected() { return WiFi.status() == WL_CONNECTED; }

void releaseRadio() {
  if (WiFi.getMode() == WIFI_MODE_NULL) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
}

bool request(const char* url, const char* method, const uint8_t* body, size_t bodyLen, const char* contentType,
             const char* contentEncoding, const FanqieProtocol::CookieJar* cookies,
             const FanqieCrypto::SignHeaders* sign, const char* accept, const char* destPath, Response& response,
             FanqieProtocol::CookieJar* updateCookies, const AbortFn abort, void* abortContext) {
  response = {};
  char path[96];
  char heap[48];
  copyPath(url ? url : "", path, sizeof(path));
  heapLine(heap, sizeof(heap));
  if (!url || !destPath || !connected()) {
    char line[160];
    snprintf(line, sizeof(line), "http skip %s %s connected=%u", method ? method : "GET", path, connected() ? 1U : 0U);
    note(line);
    response.failure = Failure::Network;
    return false;
  }
  const IPAddress local = WiFi.localIP();
  char host[96];
  IPAddress resolved;
  const bool haveHost = copyHost(url, host, sizeof(host));
  const bool resolvedOk = haveHost && WiFi.hostByName(host, resolved) == 1;
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  {
    char line[240];
    snprintf(line, sizeof(line),
             "dns %s ip=%u.%u.%u.%u rssi=%d ok=%u resolved=%u.%u.%u.%u free=%u largest=%u psram=%u task=%s hw=%u", path,
             local[0], local[1], local[2], local[3], WiFi.RSSI(), resolvedOk ? 1U : 0U, resolved[0], resolved[1],
             resolved[2], resolved[3], static_cast<unsigned>(internal.freeBytes),
             static_cast<unsigned>(internal.largestBlockBytes), static_cast<unsigned>(psram.freeBytes),
             pcTaskGetName(nullptr), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
    note(line);
  }
  if (local[0] == 0 || !resolvedOk) {
    response.failure = Failure::Network;
    return false;
  }
  note("tls start");
  const std::string temp = std::string(destPath) + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("FQ", temp, file)) {
    note("http file open failed");
    response.failure = Failure::Network;
    return false;
  }
  WriteState state{&file, 0, false};
  WeReadHttpClient::Header headers[12];
  size_t headerCount = 0;
  auto add = [&](const char* name, const char* value) {
    if (!value || !value[0] || headerCount >= 12) return;
    headers[headerCount++] = {name, value};
  };
  add("User-Agent", sign ? "com.dragon.read" : "Mozilla/5.0");
  add("Accept", accept ? accept : "application/json,text/plain,*/*");
  add("Origin", "https://fanqienovel.com");
  add("Referer", "https://fanqienovel.com/");
  if (cookies && cookies->header[0]) add("Cookie", cookies->header);
  if (contentType) add("Content-Type", contentType);
  if (contentEncoding) add("Content-Encoding", contentEncoding);
  if (sign) {
    add("x-argus", sign->argus);
    add("x-ladon", sign->ladon);
    add("x-khronos", sign->khronos);
    add("x-ss-req-ticket", sign->ticket);
    if (sign->stub[0]) add("x-ss-stub", sign->stub);
  }
  WeReadHttpClient::RequestOptions options;
  options.method = method ? method : "GET";
  options.body = body;
  options.bodySize = bodyLen;
  options.headers = headers;
  options.headerCount = headerCount;
  options.timeoutMs = 20000;
  options.readBuffer = ioBuffer;
  options.readBufferSize = sizeof(ioBuffer);
  options.shouldAbort = abort;
  options.abortContext = abortContext;
  int status = 0;
  note("request sent");
  const auto result = WeReadHttpClient::request(
      url, options, [&](const uint8_t* data, size_t len) { return onData(data, len, &state); },
      [&](const char* name, const char* value) {
        if (updateCookies && name && value && strcasecmp(name, "set-cookie") == 0)
          updateCookies->absorbSetCookie(value);
      },
      status);
  file.flush();
  file = HalFile{};
  response.status = status;
  response.bytes = state.written;
  response.network = result != WeReadHttpClient::Result::NetworkError;
  note("tls end");
  heapLine(heap, sizeof(heap));
  {
    char line[240];
    snprintf(line, sizeof(line), "http end %s result=%u status=%d bytes=%u overflow=%u %s", path,
             static_cast<unsigned>(result), status, static_cast<unsigned>(state.written), state.overflow ? 1U : 0U,
             heap);
    note(line);
  }
  if (result == WeReadHttpClient::Result::Cancelled) {
    Storage.remove(temp.c_str());
    response.failure = Failure::Cancelled;
    return false;
  }
  const bool httpOk = result == WeReadHttpClient::Result::Ok && !state.overflow && status >= 200 && status < 300;
  if (!httpOk) {
    if (result == WeReadHttpClient::Result::Ok && !state.overflow && state.written > 0) {
      Storage.remove(destPath);
      if (Storage.rename(temp.c_str(), destPath)) {
        strncpy(response.path, destPath, sizeof(response.path) - 1);
        response.failure = Failure::Http;
        return false;
      }
    }
    Storage.remove(temp.c_str());
    response.failure = state.overflow ? Failure::Memory : Failure::Network;
    if (result == WeReadHttpClient::Result::Ok && status > 0) response.failure = Failure::Http;
    return false;
  }
  Storage.remove(destPath);
  if (!Storage.rename(temp.c_str(), destPath)) {
    note("http rename failed");
    response.failure = Failure::Network;
    return false;
  }
  strncpy(response.path, destPath, sizeof(response.path) - 1);
  return true;
}

bool readFile(const char* path, char* buffer, size_t cap, size_t& length) {
  length = 0;
  if (!path || !buffer || cap < 2) return false;
  HalFile file;
  if (!Storage.openFileForRead("FQ", path, file)) return false;
  const size_t n = file.read(buffer, cap - 1);
  buffer[n] = '\0';
  length = n;
  return n + 1 < cap;
}

}  // namespace FanqieNet

#endif
