#include "FanqieNet.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kNetOmitted = 0;
}

#else

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WeReadHttpClient.h>
#include <WiFi.h>

#include <cstring>
#include <string>
#include <strings.h>

namespace FanqieNet {
namespace {

constexpr size_t kMaxBody = 256 * 1024;

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

}  // namespace

bool connected() { return WiFi.status() == WL_CONNECTED; }

bool request(const char* url, const char* method, const uint8_t* body, size_t bodyLen, const char* contentType,
             const char* contentEncoding, const FanqieProtocol::CookieJar* cookies,
             const FanqieCrypto::SignHeaders* sign, const char* accept, const char* destPath, Response& response,
             FanqieProtocol::CookieJar* updateCookies) {
  response = {};
  if (!url || !destPath || !connected()) return false;
  const std::string temp = std::string(destPath) + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("FQ", temp, file)) return false;
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
  int status = 0;
  const auto result = WeReadHttpClient::request(
      url, options,
      [&](const uint8_t* data, size_t len) { return onData(data, len, &state); },
      [&](const char* name, const char* value) {
        if (updateCookies && name && value && strcasecmp(name, "set-cookie") == 0) updateCookies->absorbSetCookie(value);
      },
      status);
  file.flush();
  file = HalFile{};
  response.status = status;
  response.network = result != WeReadHttpClient::Result::NetworkError;
  if (state.overflow || result != WeReadHttpClient::Result::Ok) {
    Storage.remove(temp.c_str());
    LOG_ERR("FQ", "request failed status=%d overflow=%u", status, state.overflow ? 1U : 0U);
    return false;
  }
  Storage.remove(destPath);
  if (!Storage.rename(temp.c_str(), destPath)) return false;
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
