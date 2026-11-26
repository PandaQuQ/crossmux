#pragma once

#include "FanqieCrypto.h"
#include "FanqieProtocol.h"

#include <cstddef>
#include <cstdint>

namespace FanqieNet {

struct Response {
  int status = 0;
  bool network = false;
  char path[64] = {};
};

bool connected();
bool request(const char* url, const char* method, const uint8_t* body, size_t bodyLen, const char* contentType,
             const char* contentEncoding, const FanqieProtocol::CookieJar* cookies,
             const FanqieCrypto::SignHeaders* sign, const char* accept, const char* destPath, Response& response,
             FanqieProtocol::CookieJar* updateCookies);
bool readFile(const char* path, char* buffer, size_t cap, size_t& length);

}  // namespace FanqieNet
