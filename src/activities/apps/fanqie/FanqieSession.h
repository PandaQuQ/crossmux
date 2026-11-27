#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "FanqieProtocol.h"

namespace FanqieSession {

enum class Status : uint8_t {
  Ok,
  Waiting,
  Offline,
  Failed,
  Expired,
  NeedAccount,
  Memory,
  Network,
  Http,
  Parse,
  Gzip,
  Qr,
  Server,
  Cancelled
};

struct Failure {
  int http = 0;
  char hint[40] = {};
};

// The PNG buffer is allocated after the TLS call returns, so the handshake
// still has a contiguous block for wolfSSL.
Status startLogin(FanqieProtocol::CookieJar& jar, char* token, size_t tokenCap, std::unique_ptr<uint8_t[]>& png,
                  size_t& pngLen, Failure* failure = nullptr, bool (*abort)(void*) = nullptr,
                  void* abortContext = nullptr);
Status pollLogin(FanqieProtocol::CookieJar& jar, const char* token, bool (*abort)(void*) = nullptr,
                 void* abortContext = nullptr);
bool storeAccount(const FanqieProtocol::CookieJar& jar);
Status syncShelf();
Status prepareOpen(const char* epubPath);
Status uploadProgress(const char* epubPath);

}  // namespace FanqieSession
