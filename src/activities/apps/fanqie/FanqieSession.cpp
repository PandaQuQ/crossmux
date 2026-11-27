#include "FanqieSession.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kSessionOmitted = 0;
}

#else

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <RecentBooksStore.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include "FanqieCrypto.h"
#include "FanqieEpub.h"
#include "FanqieLog.h"
#include "FanqieNet.h"
#include "FanqieStore.h"
#include "ReadingStatsStore.h"
#include "activities/reader/ProgressFile.h"
#include "util/TimeUtils.h"

namespace FanqieSession {
namespace {

constexpr char kScratch[] = "/.crosspoint/fanqie/scratch.json";

bool loadJson(const char* path, std::unique_ptr<char[]>& buffer, size_t& length) {
  HalFile file;
  if (!Storage.openFileForRead("FQ", path, file)) return false;
  const size_t size = file.size();
  if (size == 0 || size > 160 * 1024) return false;
  buffer = makeUniqueNoThrow<char[]>(size + 1);
  if (!buffer) {
    LOG_ERR("FQ", "OOM: response %u", static_cast<unsigned>(size + 1));
    return false;
  }
  if (file.read(buffer.get(), size) != size) return false;
  buffer[size] = '\0';
  length = size;
  return true;
}

bool getToFile(const char* url, const FanqieProtocol::CookieJar* cookies, const FanqieCrypto::SignHeaders* sign) {
  FanqieNet::Response response;
  return FanqieNet::request(url, "GET", nullptr, 0, nullptr, nullptr, cookies, sign, nullptr, kScratch, response,
                            nullptr);
}

uint32_t nowUnix() { return TimeUtils::getCurrentValidTimestamp(); }

void fillRandom(uint8_t* out, size_t len) {
  for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>(esp_random() & 0xff);
}

bool ensureDeviceId(char* out, size_t cap) {
  if (FanqieStore::loadDeviceId(out, cap)) return true;
  if (cap < 20) return false;
  out[0] = '7';
  for (int i = 1; i < 19; ++i) out[i] = static_cast<char>('0' + (esp_random() % 10));
  out[19] = '\0';
  return FanqieStore::saveDeviceId(out);
}

bool hexEncode(const uint8_t* in, size_t len, char* out, size_t cap) {
  if (cap < len * 2 + 1) return false;
  static constexpr char kDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    out[i * 2] = kDigits[in[i] >> 4];
    out[i * 2 + 1] = kDigits[in[i] & 0x0f];
  }
  out[len * 2] = '\0';
  return true;
}

bool ensureContentKey(const char* deviceId, char* keyHex, size_t cap) {
  if (FanqieStore::loadContentKey(keyHex, cap)) return true;
  uint8_t iv[16];
  fillRandom(iv, sizeof(iv));
  char content[160];
  size_t contentLen = 0;
  if (!FanqieCrypto::encryptRegisterKeyRequest(deviceId, iv, content, sizeof(content), contentLen)) return false;
  char json[220];
  const int jsonLen = snprintf(json, sizeof(json), "{\"content\":\"%s\"}", content);
  if (jsonLen <= 0 || static_cast<size_t>(jsonLen) >= sizeof(json)) return false;
  uint8_t gz[320];
  size_t gzLen = 0;
  if (!FanqieCrypto::gzipStore(reinterpret_cast<const uint8_t*>(json), static_cast<size_t>(jsonLen), gz, sizeof(gz),
                               gzLen))
    return false;
  char query[256];
  char url[512];
  if (!FanqieProtocol::registerKeyQuery(deviceId, query, sizeof(query)) ||
      !FanqieProtocol::registerKeyUrl(query, url, sizeof(url)))
    return false;
  FanqieCrypto::SignMaterial material;
  material.query = query;
  material.body = gz;
  material.bodyLen = gzLen;
  material.timestamp = nowUnix() ? nowUnix() : 1700000000;
  material.deviceId = deviceId;
  material.installId = "0";
  fillRandom(reinterpret_cast<uint8_t*>(&material.randField), sizeof(material.randField));
  fillRandom(material.ladonRandom, sizeof(material.ladonRandom));
  FanqieCrypto::SignHeaders headers;
  if (!FanqieCrypto::signHeaders(material, headers)) return false;
  FanqieNet::Response response;
  if (!FanqieNet::request(url, "POST", gz, gzLen, nullptr, "gzip", nullptr, &headers, nullptr, kScratch, response,
                          nullptr))
    return false;
  std::unique_ptr<char[]> body;
  size_t length = 0;
  char encrypted[128];
  if (!loadJson(kScratch, body, length) ||
      !FanqieProtocol::findJsonString(body.get(), length, "key", encrypted, sizeof(encrypted)))
    return false;
  uint8_t plain[64];
  size_t plainLen = 0;
  if (!FanqieCrypto::decryptRegisterKey(encrypted, plain, sizeof(plain), plainLen) || plainLen == 0 || plainLen > 32)
    return false;
  if (!hexEncode(plain, plainLen, keyHex, cap) || !FanqieStore::saveContentKey(keyHex)) return false;
  return true;
}

bool saveChapterText(const char* bookId, uint32_t index, const char* text, size_t len) {
  char path[128];
  char dir[96];
  snprintf(dir, sizeof(dir), "/.crosspoint/fanqie/books/%s/c", bookId);
  if (!FanqieStore::chapterTextPath(bookId, index, path, sizeof(path)) || !Storage.mkdir(dir)) return false;
  HalFile file;
  if (!Storage.openFileForWrite("FQ", path, file)) return false;
  return file.write(text, len) == len;
}

bool writeChapterFromContent(const char* bookId, uint32_t index, const char* content, size_t len, const char* keyHex) {
  const auto kind = FanqieProtocol::classifyChapter(content, len);
  auto text = makeUniqueNoThrow<char[]>(48 * 1024);
  if (!text) {
    LOG_ERR("FQ", "OOM: chapter text");
    return false;
  }
  size_t textLen = 0;
  if (kind == FanqieProtocol::ChapterKind::Text) {
    if (!FanqieProtocol::stripChapterText(content, len, text.get(), 48 * 1024, textLen)) return false;
  } else if (kind == FanqieProtocol::ChapterKind::Cipher && keyHex && keyHex[0]) {
    auto plain = makeUniqueNoThrow<uint8_t[]>(48 * 1024);
    if (!plain) {
      LOG_ERR("FQ", "OOM: chapter plain");
      return false;
    }
    size_t plainLen = 0;
    if (!FanqieCrypto::decryptContent(content, len, keyHex, plain.get(), 48 * 1024, plainLen)) return false;
    if (!FanqieProtocol::stripChapterText(reinterpret_cast<char*>(plain.get()), plainLen, text.get(), 48 * 1024,
                                          textLen)) {
      if (plainLen >= 48 * 1024) return false;
      memcpy(text.get(), plain.get(), plainLen);
      textLen = plainLen;
    }
  } else {
    return false;
  }
  return textLen > 0 && saveChapterText(bookId, index, text.get(), textLen);
}

bool downloadAppChapter(const char* bookId, const char* itemId, uint32_t index) {
  char deviceId[24];
  char keyHex[80];
  if (!ensureDeviceId(deviceId, sizeof(deviceId)) || !ensureContentKey(deviceId, keyHex, sizeof(keyHex))) return false;
  char query[320];
  char url[640];
  if (!FanqieProtocol::appChapterQuery(deviceId, itemId, query, sizeof(query)) ||
      !FanqieProtocol::appChapterUrl(query, url, sizeof(url)))
    return false;
  FanqieCrypto::SignMaterial material;
  material.query = query;
  material.timestamp = nowUnix() ? nowUnix() : 1700000000;
  material.deviceId = deviceId;
  material.installId = "0";
  fillRandom(reinterpret_cast<uint8_t*>(&material.randField), sizeof(material.randField));
  fillRandom(material.ladonRandom, sizeof(material.ladonRandom));
  FanqieCrypto::SignHeaders headers;
  if (!FanqieCrypto::signHeaders(material, headers) || !getToFile(url, nullptr, &headers)) return false;
  std::unique_ptr<char[]> body;
  size_t length = 0;
  if (!loadJson(kScratch, body, length)) return false;
  const char* content = strstr(body.get(), "\"content\"");
  if (!content) return false;
  char extracted[48];
  // Content is a long base64 or HTML string. Reuse the parser on the whole document.
  auto held = makeUniqueNoThrow<char[]>(length + 1);
  if (!held) return false;
  if (!FanqieProtocol::findJsonString(body.get(), length, "content", held.get(), length + 1)) return false;
  (void)extracted;
  return writeChapterFromContent(bookId, index, held.get(), strlen(held.get()), keyHex);
}

bool downloadChapter(const FanqieProtocol::CookieJar& jar, const char* bookId, const char* itemId, uint32_t index) {
  if (FanqieStore::hasChapterText(bookId, index)) return true;
  char url[256];
  if (FanqieProtocol::webChapterUrl(bookId, itemId, url, sizeof(url)) && getToFile(url, &jar, nullptr)) {
    std::unique_ptr<char[]> body;
    size_t length = 0;
    auto content = makeUniqueNoThrow<char[]>(96 * 1024);
    if (loadJson(kScratch, body, length) && content &&
        FanqieProtocol::findJsonString(body.get(), length, "content", content.get(), 96 * 1024)) {
      const auto kind = FanqieProtocol::classifyChapter(content.get(), strlen(content.get()));
      if (kind == FanqieProtocol::ChapterKind::Text &&
          writeChapterFromContent(bookId, index, content.get(), strlen(content.get()), nullptr))
        return true;
    }
  }
  return downloadAppChapter(bookId, itemId, index);
}

std::string cacheDir(const std::string& path) {
  return "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(path));
}

bool writeSpine(const std::string& epubPath, uint16_t spine, uint16_t page) {
  const std::string cache = cacheDir(epubPath);
  if (!Storage.mkdir(cache.c_str())) return false;
  uint8_t data[4] = {static_cast<uint8_t>(spine), static_cast<uint8_t>(spine >> 8), static_cast<uint8_t>(page),
                     static_cast<uint8_t>(page >> 8)};
  return ProgressFile::writeAtomic(cache, data, sizeof(data));
}

bool readSpine(const std::string& epubPath, uint16_t& spine, uint16_t& page, uint16_t& pageCount) {
  spine = 0;
  page = 0;
  pageCount = 0;
  HalFile file;
  const std::string path = cacheDir(epubPath) + "/progress.bin";
  if (!Storage.openFileForRead("FQ", path, file)) return false;
  uint8_t data[10];
  const size_t n = file.read(data, sizeof(data));
  if (n != 4 && n != 6 && n != 10) return false;
  spine = static_cast<uint16_t>(data[0] | (data[1] << 8));
  page = static_cast<uint16_t>(data[2] | (data[3] << 8));
  if (n >= 6) pageCount = static_cast<uint16_t>(data[4] | (data[5] << 8));
  return true;
}

void remember(const FanqieStore::BookMeta& meta, int percent) {
  RECENT_BOOKS.addBook(meta.epubPath, meta.title, meta.author, "");
  if (READING_STATS.noteExternalProgress(meta.epubPath, meta.title, meta.author,
                                         static_cast<uint8_t>(percent < 0 ? 0 : percent)))
    READING_STATS.saveToFile();
}

}  // namespace

void rememberFailure(Failure* failure, int http, const char* hint) {
  if (!failure) return;
  failure->http = http;
  failure->hint[0] = '\0';
  if (!hint || !hint[0]) return;
  snprintf(failure->hint, sizeof(failure->hint), "%s", hint);
}

Status fromTransport(const FanqieNet::Response& response, Failure* failure) {
  rememberFailure(failure, response.status, nullptr);
  if (response.failure == FanqieNet::Failure::Memory) return Status::Memory;
  if (response.failure == FanqieNet::Failure::Http) return Status::Http;
  if (response.failure == FanqieNet::Failure::Cancelled) return Status::Cancelled;
  return Status::Network;
}

Status startLogin(FanqieProtocol::CookieJar& jar, char* token, size_t tokenCap, std::unique_ptr<uint8_t[]>& png,
                  size_t& pngLen, Failure* failure, bool (*abort)(void*), void* abortContext) {
  png.reset();
  pngLen = 0;
  if (failure) *failure = {};
  if (!FanqieNet::connected()) return Status::Offline;
  jar.clear();
  char url[512];
  FanqieNet::Response response;
  FanqieLog::line("qr fetch");
  if (!FanqieProtocol::qrStartUrl(url, sizeof(url)) ||
      !FanqieNet::request(url, "GET", nullptr, 0, nullptr, nullptr, &jar, nullptr, nullptr, kScratch, response, &jar,
                          abort, abortContext)) {
    Status status = fromTransport(response, failure);
    if (status == Status::Http) {
      std::unique_ptr<char[]> body;
      size_t length = 0;
      if (loadJson(kScratch, body, length) && failure &&
          FanqieProtocol::copyLoginHint(body.get(), length, failure->hint, sizeof(failure->hint)))
        status = Status::Server;
    }
    return status;
  }
  std::unique_ptr<char[]> body;
  size_t length = 0;
  uint32_t expire = 0;
  if (!loadJson(kScratch, body, length)) {
    FanqieLog::line("qr body unreadable");
    rememberFailure(failure, response.status, nullptr);
    return Status::Parse;
  }
  if (length >= 2 && static_cast<uint8_t>(body[0]) == 0x1f && static_cast<uint8_t>(body[1]) == 0x8b) {
    FanqieLog::line("qr body gzip");
    rememberFailure(failure, response.status, nullptr);
    return Status::Gzip;
  }
  constexpr size_t kPngCap = 16 * 1024;
  png = makeUniqueNoThrow<uint8_t[]>(kPngCap);
  if (!png) {
    FanqieLog::line("qr image oom");
    rememberFailure(failure, response.status, nullptr);
    return Status::Memory;
  }
  if (!FanqieProtocol::parseQrStart(body.get(), length, token, tokenCap, png.get(), kPngCap, pngLen, expire)) {
    char hint[40];
    const bool hinted = FanqieProtocol::copyLoginHint(body.get(), length, hint, sizeof(hint));
    char line[96];
    snprintf(line, sizeof(line), "qr parse fail bytes=%u head=%02x%02x hinted=%u", static_cast<unsigned>(length),
             static_cast<unsigned>(static_cast<uint8_t>(body[0])), static_cast<unsigned>(static_cast<uint8_t>(body[1])),
             hinted ? 1U : 0U);
    FanqieLog::line(line);
    rememberFailure(failure, response.status, hinted ? hint : nullptr);
    return hinted ? Status::Server : Status::Parse;
  }
  if (pngLen <= 8) {
    char line[64];
    snprintf(line, sizeof(line), "qr image short bytes=%u", static_cast<unsigned>(pngLen));
    FanqieLog::line(line);
    rememberFailure(failure, response.status, nullptr);
    return Status::Qr;
  }
  char line[64];
  snprintf(line, sizeof(line), "qr ok png=%u", static_cast<unsigned>(pngLen));
  FanqieLog::line(line);
  return Status::Ok;
}

Status pollLogin(FanqieProtocol::CookieJar& jar, const char* token, bool (*abort)(void*), void* abortContext) {
  if (!FanqieNet::connected()) return Status::Offline;
  char url[640];
  if (!FanqieProtocol::qrPollUrl(token, url, sizeof(url))) return Status::Failed;
  FanqieNet::Response response;
  if (!FanqieNet::request(url, "GET", nullptr, 0, nullptr, nullptr, &jar, nullptr, nullptr, kScratch, response, &jar,
                          abort, abortContext)) {
    char line[64];
    snprintf(line, sizeof(line), "qr poll fail status=%d", response.status);
    FanqieLog::line(line);
    return fromTransport(response, nullptr);
  }
  if (jar.has("sessionid")) {
    char progress[128];
    if (!FanqieProtocol::progressUrl(progress, sizeof(progress)) || !getToFile(progress, &jar, nullptr))
      return Status::Failed;
    return Status::Ok;
  }
  std::unique_ptr<char[]> body;
  size_t length = 0;
  if (!loadJson(kScratch, body, length)) return Status::Failed;
  const auto poll = FanqieProtocol::parseLoginPoll(body.get(), length);
  if (poll == FanqieProtocol::LoginPoll::Expired) return Status::Expired;
  if (poll == FanqieProtocol::LoginPoll::Failed || poll == FanqieProtocol::LoginPoll::Cancelled) return Status::Failed;
  return Status::Waiting;
}

bool storeAccount(const FanqieProtocol::CookieJar& jar) {
  if (!jar.has("sessionid")) return false;
  auto accounts = makeUniqueNoThrow<FanqieStore::Account[]>(FanqieStore::kMaxAccounts);
  if (!accounts) {
    LOG_ERR("FQ", "OOM: store account");
    return false;
  }
  int count = 0;
  int active = -1;
  if (!FanqieStore::loadAccounts(accounts.get(), count, active)) return false;
  if (count >= FanqieStore::kMaxAccounts) return false;
  FanqieStore::Account& account = accounts[count];
  snprintf(account.nickname, sizeof(account.nickname), "Fanqie %d", count + 1);
  strncpy(account.cookie, jar.header, sizeof(account.cookie) - 1);
  active = count;
  ++count;
  return FanqieStore::saveAccounts(accounts.get(), count, active);
}

Status syncShelf() {
  if (!FanqieNet::connected()) return Status::Offline;
  FanqieStore::Account account;
  if (!FanqieStore::loadActiveAccount(account)) return Status::NeedAccount;
  FanqieProtocol::CookieJar jar;
  strncpy(jar.header, account.cookie, sizeof(jar.header) - 1);
  char url[320];
  if (!FanqieProtocol::shelfUrl(url, sizeof(url)) || !getToFile(url, &jar, nullptr)) return Status::Failed;
  std::unique_ptr<char[]> shelf;
  size_t shelfLen = 0;
  if (!loadJson(kScratch, shelf, shelfLen)) return Status::Failed;
  FanqieProtocol::ShelfBook books[FanqieStore::kMaxBooks];
  int bookCount = 0;
  struct IdCtx {
    FanqieProtocol::ShelfBook* books;
    int* count;
  } ctx{books, &bookCount};
  if (!FanqieProtocol::parseShelfIds(
          shelf.get(), shelfLen,
          [](void* raw, const char* id) {
            auto* state = static_cast<IdCtx*>(raw);
            if (*state->count >= FanqieStore::kMaxBooks) return true;
            strncpy(state->books[*state->count].bookId, id, 23);
            strncpy(state->books[*state->count].itemId, "0", 2);
            ++*state->count;
            return true;
          },
          &ctx))
    return Status::Failed;
  if (bookCount == 0) return Status::Ok;
  char body[2048];
  if (!FanqieProtocol::multidetailBody(books, bookCount, body, sizeof(body)) ||
      !FanqieProtocol::multidetailUrl(url, sizeof(url)))
    return Status::Failed;
  FanqieNet::Response response;
  if (!FanqieNet::request(url, "POST", reinterpret_cast<const uint8_t*>(body), strlen(body), "application/json",
                          nullptr, &jar, nullptr, nullptr, kScratch, response, nullptr))
    return Status::Failed;
  std::unique_ptr<char[]> detail;
  size_t detailLen = 0;
  FanqieProtocol::ShelfBook details[FanqieStore::kMaxBooks];
  int detailCount = 0;
  if (!loadJson(kScratch, detail, detailLen) ||
      !FanqieProtocol::parseBookDetails(detail.get(), detailLen, details, FanqieStore::kMaxBooks, detailCount))
    return Status::Failed;
  if (!FanqieProtocol::progressUrl(url, sizeof(url)) || !getToFile(url, &jar, nullptr)) return Status::Failed;
  std::unique_ptr<char[]> progress;
  size_t progressLen = 0;
  if (!loadJson(kScratch, progress, progressLen)) return Status::Failed;
  struct ProgressCtx {
    FanqieProtocol::ShelfBook* books;
    int count;
  } progressCtx{books, bookCount};
  FanqieProtocol::parseProgress(
      progress.get(), progressLen,
      [](void* raw, const FanqieProtocol::ProgressRecord& record) {
        auto* state = static_cast<ProgressCtx*>(raw);
        for (int i = 0; i < state->count; ++i) {
          if (strcmp(state->books[i].bookId, record.bookId) == 0) {
            strncpy(state->books[i].itemId, record.itemId, sizeof(state->books[i].itemId) - 1);
          }
        }
        return true;
      },
      &progressCtx);
  for (int i = 0; i < bookCount; ++i) {
    for (int d = 0; d < detailCount; ++d) {
      if (strcmp(details[d].bookId, books[i].bookId) == 0) {
        strncpy(books[i].title, details[d].title, sizeof(books[i].title) - 1);
        strncpy(books[i].author, details[d].author, sizeof(books[i].author) - 1);
        books[i].chapterCount = details[d].chapterCount;
      }
    }
    uint32_t existing = 0;
    if (!FanqieStore::chapterCount(books[i].bookId, existing) || existing == 0) {
      if (!FanqieProtocol::directoryUrl(books[i].bookId, url, sizeof(url)) || !getToFile(url, &jar, nullptr)) continue;
      std::unique_ptr<char[]> directory;
      size_t directoryLen = 0;
      if (!loadJson(kScratch, directory, directoryLen)) continue;
      constexpr uint32_t kMaxChapters = 600;
      auto records = makeUniqueNoThrow<FanqieStore::ChapterRecord[]>(kMaxChapters);
      if (!records) {
        LOG_ERR("FQ", "OOM: chapter index");
        return Status::Failed;
      }
      struct ChapterCtx {
        FanqieStore::ChapterRecord* records;
        uint32_t count;
      } chapterCtx{records.get(), 0};
      FanqieProtocol::parseChapters(
          directory.get(), directoryLen,
          [](void* raw, const FanqieProtocol::ChapterRef& chapter) {
            auto* state = static_cast<ChapterCtx*>(raw);
            if (state->count >= 600) return true;
            auto& record = state->records[state->count++];
            strncpy(record.itemId, chapter.itemId, sizeof(record.itemId) - 1);
            strncpy(record.title, chapter.title, sizeof(record.title) - 1);
            return true;
          },
          &chapterCtx);
      if (!FanqieStore::saveChapters(books[i].bookId, records.get(), chapterCtx.count)) continue;
      books[i].chapterCount = chapterCtx.count;
    }
    FanqieStore::BookMeta meta;
    if (!FanqieStore::loadMeta(books[i].bookId, meta)) {
      strncpy(meta.bookId, books[i].bookId, sizeof(meta.bookId) - 1);
    }
    strncpy(meta.title, books[i].title, sizeof(meta.title) - 1);
    strncpy(meta.author, books[i].author, sizeof(meta.author) - 1);
    FanqieStore::epubPathFor(meta.bookId, meta.epubPath, sizeof(meta.epubPath));
    meta.chapterCount = books[i].chapterCount;
    if (books[i].itemId[0] && strcmp(books[i].itemId, "0") != 0) strncpy(meta.cloudItem, books[i].itemId, 23);
    if (!FanqieStore::saveMeta(meta)) continue;
    uint32_t index = 0;
    FanqieStore::ChapterRecord record;
    while (FanqieStore::readChapter(meta.bookId, index, record)) {
      if (strcmp(record.itemId, meta.cloudItem) == 0) break;
      ++index;
      if (index > 5000) break;
    }
    if (meta.cloudItem[0]) downloadChapter(jar, meta.bookId, meta.cloudItem, index);
    FanqieEpub::build(meta.bookId);
    remember(meta, 0);
  }
  return Status::Ok;
}

Status prepareOpen(const char* epubPath) {
  FanqieStore::BookMeta meta;
  if (!epubPath || !FanqieStore::findByEpubPath(epubPath, meta)) return Status::Failed;
  if (!FanqieNet::connected()) {
    remember(meta, FanqieProtocol::shelfPercent(meta.spine, static_cast<int>(meta.chapterCount), 0, false));
    return Status::Offline;
  }
  if (meta.dirty) uploadProgress(epubPath);
  FanqieStore::loadMeta(meta.bookId, meta);
  FanqieStore::Account account;
  if (!FanqieStore::loadActiveAccount(account)) {
    remember(meta, 0);
    return Status::NeedAccount;
  }
  FanqieProtocol::CookieJar jar;
  strncpy(jar.header, account.cookie, sizeof(jar.header) - 1);
  char url[256];
  if (FanqieProtocol::progressUrl(url, sizeof(url)) && getToFile(url, &jar, nullptr)) {
    std::unique_ptr<char[]> body;
    size_t length = 0;
    if (loadJson(kScratch, body, length)) {
      struct Ctx {
        const char* bookId;
        FanqieProtocol::ProgressRecord record;
        bool found;
      } ctx{meta.bookId, {}, false};
      FanqieProtocol::parseProgress(
          body.get(), length,
          [](void* raw, const FanqieProtocol::ProgressRecord& record) {
            auto* state = static_cast<Ctx*>(raw);
            if (strcmp(record.bookId, state->bookId) == 0) {
              state->record = record;
              state->found = true;
            }
            return true;
          },
          &ctx);
      if (ctx.found && FanqieProtocol::chooseProgress(meta.dirty, meta.localUnix, ctx.record.readUnix) ==
                           FanqieProtocol::ProgressChoice::ApplyCloud) {
        uint32_t count = 0;
        FanqieStore::chapterCount(meta.bookId, count);
        int spine = 0;
        FanqieStore::ChapterRecord record;
        for (uint32_t i = 0; i < count; ++i) {
          if (!FanqieStore::readChapter(meta.bookId, i, record)) break;
          if (strcmp(record.itemId, ctx.record.itemId) == 0) {
            spine = static_cast<int>(i);
            break;
          }
        }
        meta.spine = static_cast<uint16_t>(spine);
        meta.page = 0;
        meta.cloudUnix = ctx.record.readUnix;
        strncpy(meta.cloudItem, ctx.record.itemId, sizeof(meta.cloudItem) - 1);
        meta.dirty = 0;
        FanqieStore::saveMeta(meta);
        writeSpine(epubPath, meta.spine, 0);
        downloadChapter(jar, meta.bookId, ctx.record.itemId, static_cast<uint32_t>(spine));
        FanqieEpub::build(meta.bookId);
        remember(meta, FanqieProtocol::shelfPercent(spine, static_cast<int>(count), ctx.record.rawProgress,
                                                    ctx.record.tenThousand));
        return Status::Ok;
      }
    }
  }
  remember(meta, FanqieProtocol::shelfPercent(meta.spine, static_cast<int>(meta.chapterCount), 0, false));
  return Status::Ok;
}

Status uploadProgress(const char* epubPath) {
  FanqieStore::BookMeta meta;
  if (!epubPath || !FanqieStore::findByEpubPath(epubPath, meta)) return Status::Failed;
  uint16_t spine = meta.spine;
  uint16_t page = meta.page;
  uint16_t pageCount = meta.pageCount;
  readSpine(epubPath, spine, page, pageCount);
  FanqieStore::ChapterRecord chapter;
  if (!FanqieStore::readChapter(meta.bookId, spine, chapter)) {
    // A card that only has the EPUB has no chapter map yet. Keep the reader
    // progress already saved on exit and remember the book for the next sync.
    meta.spine = spine;
    meta.page = page;
    meta.pageCount = pageCount;
    meta.dirty = 1;
    FanqieStore::saveMeta(meta);
    return Status::Offline;
  }
  meta.spine = spine;
  meta.page = page;
  meta.pageCount = pageCount;
  meta.localUnix = nowUnix();
  strncpy(meta.cloudItem, chapter.itemId, sizeof(meta.cloudItem) - 1);
  const double fraction = pageCount > 0 ? static_cast<double>(page + 1) / static_cast<double>(pageCount) : 0;
  if (!FanqieNet::connected()) {
    meta.dirty = 1;
    FanqieStore::saveMeta(meta);
    remember(meta, FanqieProtocol::shelfPercent(spine, static_cast<int>(meta.chapterCount), fraction, false));
    return Status::Offline;
  }
  FanqieStore::Account account;
  if (!FanqieStore::loadActiveAccount(account)) return Status::NeedAccount;
  FanqieProtocol::CookieJar jar;
  strncpy(jar.header, account.cookie, sizeof(jar.header) - 1);
  char body[256];
  char url[160];
  if (!FanqieProtocol::progressUpdateBody(meta.bookId, chapter.itemId, spine, fraction, meta.localUnix, body,
                                          sizeof(body)) ||
      !FanqieProtocol::updateProgressUrl(url, sizeof(url)))
    return Status::Failed;
  FanqieNet::Response response;
  if (!FanqieNet::request(url, "POST", reinterpret_cast<const uint8_t*>(body), strlen(body), "application/json",
                          nullptr, &jar, nullptr, nullptr, kScratch, response, nullptr)) {
    meta.dirty = 1;
    FanqieStore::saveMeta(meta);
    return Status::Failed;
  }
  meta.dirty = 0;
  meta.cloudUnix = meta.localUnix;
  FanqieStore::saveMeta(meta);
  remember(meta, FanqieProtocol::shelfPercent(spine, static_cast<int>(meta.chapterCount), fraction, false));
  return Status::Ok;
}

}  // namespace FanqieSession

#endif
