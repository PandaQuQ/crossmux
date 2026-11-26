#pragma once

#include "FanqieProtocol.h"

#include <cstddef>
#include <cstdint>

namespace FanqieSession {

enum class Status : uint8_t { Ok, Waiting, Offline, Failed, Expired, NeedAccount };

Status startLogin(FanqieProtocol::CookieJar& jar, char* token, size_t tokenCap, uint8_t* png, size_t pngCap,
                  size_t& pngLen);
Status pollLogin(FanqieProtocol::CookieJar& jar, const char* token);
bool storeAccount(const FanqieProtocol::CookieJar& jar);
Status syncShelf();
Status prepareOpen(const char* epubPath);
Status uploadProgress(const char* epubPath);

}  // namespace FanqieSession
