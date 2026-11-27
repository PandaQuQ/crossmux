#pragma once

#include <cstddef>
#include <cstdint>

#include "FanqieCrypto.h"
#include "FanqieProtocol.h"

namespace FanqieNet {

enum class Failure : uint8_t { None, Memory, Network, Http };

struct Response {
  int status = 0;
  bool network = false;
  Failure failure = Failure::None;
  size_t bytes = 0;
  char path[64] = {};
};

bool connected();
// Drop the station the way other network activities do when they leave.
void releaseRadio();
bool request(const char* url, const char* method, const uint8_t* body, size_t bodyLen, const char* contentType,
             const char* contentEncoding, const FanqieProtocol::CookieJar* cookies,
             const FanqieCrypto::SignHeaders* sign, const char* accept, const char* destPath, Response& response,
             FanqieProtocol::CookieJar* updateCookies);
bool readFile(const char* path, char* buffer, size_t cap, size_t& length);

}  // namespace FanqieNet
