#include "FanqieStore.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kStoreOmitted = 0;
}

#else

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace FanqieStore {
namespace {

constexpr char kAccounts[] = "/.crosspoint/fanqie/accounts.bin";
constexpr char kIndex[] = "/.crosspoint/fanqie/index.bin";
constexpr char kDevice[] = "/.crosspoint/fanqie/device.bin";
constexpr char kContentKey[] = "/.crosspoint/fanqie/content.key";

struct AccountHeader {
  uint32_t magic = kAccountMagic;
  uint16_t version = kAccountVersion;
  uint16_t count = 0;
  int16_t active = -1;
  uint16_t reserved = 0;
};

struct ChapterHeader {
  uint32_t magic = kChapterMagic;
  uint16_t version = kChapterVersion;
  uint16_t recordSize = sizeof(ChapterRecord);
  uint32_t count = 0;
};

struct MetaHeader {
  uint32_t magic = kMetaMagic;
  uint16_t version = kMetaVersion;
  uint16_t bytes = sizeof(BookMeta);
};

struct IndexHeader {
  uint32_t magic = kIndexMagic;
  uint16_t version = kIndexVersion;
  uint16_t count = 0;
};

struct IndexRecord {
  char bookId[24] = {};
  char epubPath[96] = {};
};

bool writeAll(const char* path, const void* data, size_t len) {
  const std::string tmp = std::string(path) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("FQ", tmp, file)) return false;
    if (file.write(data, len) != len) return false;
    file.flush();
  }
  Storage.remove(path);
  return Storage.rename(tmp.c_str(), path);
}

bool readAll(const char* path, void* data, size_t len) {
  HalFile file;
  if (!Storage.openFileForRead("FQ", path, file)) return false;
  return file.read(data, len) == len;
}

bool bookDir(const char* bookId, char* out, size_t cap) {
  const int n = snprintf(out, cap, "/.crosspoint/fanqie/books/%s", bookId);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool idSafe(const char* bookId) {
  if (!bookId || !bookId[0] || strlen(bookId) >= 24) return false;
  for (const char* p = bookId; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
  }
  return true;
}

}  // namespace

bool ensureDir(const char* path) {
  // SdFat mkdir returns false when the directory is already there. /Fanqie
  // exists as soon as a cached book is on the card, so that must still count.
  if (Storage.exists(path)) return true;
  return Storage.mkdir(path);
}

bool ensureRoot() {
  return ensureDir("/.crosspoint") && ensureDir(kRoot) && ensureDir("/.crosspoint/fanqie/books") &&
         ensureDir("/Fanqie");
}

bool loadAccounts(Account* accounts, int& count, int& active) {
  count = 0;
  active = -1;
  if (!accounts) return false;
  AccountHeader header;
  HalFile file;
  if (!Storage.openFileForRead("FQ", kAccounts, file)) return true;
  if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != kAccountMagic ||
      header.version != kAccountVersion || header.count > kMaxAccounts)
    return false;
  active = header.active;
  for (uint16_t i = 0; i < header.count; ++i) {
    uint16_t cookieLen = 0;
    if (file.read(accounts[i].nickname, kNickname) != kNickname || file.read(&cookieLen, sizeof(cookieLen)) != 2)
      return false;
    if (cookieLen >= kCookie) return false;
    if (cookieLen && file.read(accounts[i].cookie, cookieLen) != cookieLen) return false;
    accounts[i].cookie[cookieLen] = '\0';
  }
  count = header.count;
  return true;
}

bool loadNicknames(char nicknames[][kNickname], int cap, int& count, int& active) {
  count = 0;
  active = -1;
  if (!nicknames || cap <= 0) return false;
  AccountHeader header;
  HalFile file;
  if (!Storage.openFileForRead("FQ", kAccounts, file)) return true;
  if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != kAccountMagic ||
      header.version != kAccountVersion || header.count > kMaxAccounts)
    return false;
  active = header.active;
  for (uint16_t i = 0; i < header.count; ++i) {
    char nickname[kNickname];
    uint16_t cookieLen = 0;
    if (file.read(nickname, kNickname) != kNickname || file.read(&cookieLen, sizeof(cookieLen)) != 2) return false;
    if (cookieLen >= kCookie) return false;
    if (cookieLen && !file.seek(file.position() + cookieLen)) return false;
    if (static_cast<int>(i) < cap) memcpy(nicknames[i], nickname, kNickname);
  }
  count = header.count < cap ? header.count : cap;
  if (active >= count) active = count > 0 ? 0 : -1;
  return true;
}

bool saveAccounts(const Account* accounts, int count, int active) {
  if (!accounts || count < 0 || count > kMaxAccounts || !ensureRoot()) return false;
  constexpr size_t kBytes = sizeof(AccountHeader) + kMaxAccounts * (kNickname + 2 + kCookie);
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kBytes);
  if (!buffer) {
    LOG_ERR("FQ", "OOM: account file");
    return false;
  }
  AccountHeader header;
  header.count = static_cast<uint16_t>(count);
  header.active = static_cast<int16_t>(active);
  memcpy(buffer.get(), &header, sizeof(header));
  size_t used = sizeof(header);
  for (int i = 0; i < count; ++i) {
    memcpy(buffer.get() + used, accounts[i].nickname, kNickname);
    used += kNickname;
    const uint16_t cookieLen = static_cast<uint16_t>(strnlen(accounts[i].cookie, kCookie - 1));
    memcpy(buffer.get() + used, &cookieLen, sizeof(cookieLen));
    used += sizeof(cookieLen);
    memcpy(buffer.get() + used, accounts[i].cookie, cookieLen);
    used += cookieLen;
  }
  return writeAll(kAccounts, buffer.get(), used);
}

bool loadActiveAccount(Account& account) {
  auto accounts = makeUniqueNoThrow<Account[]>(kMaxAccounts);
  if (!accounts) {
    LOG_ERR("FQ", "OOM: active account");
    return false;
  }
  int count = 0;
  int active = -1;
  if (!loadAccounts(accounts.get(), count, active) || active < 0 || active >= count) return false;
  account = accounts[active];
  return account.used();
}

bool saveChapters(const char* bookId, const ChapterRecord* records, uint32_t count) {
  if (!idSafe(bookId) || (count && !records) || !ensureRoot()) return false;
  char dir[96];
  char path[128];
  if (!bookDir(bookId, dir, sizeof(dir))) return false;
  if (!Storage.mkdir(dir)) return false;
  snprintf(path, sizeof(path), "%s/chapters.bin", dir);
  ChapterHeader header;
  header.count = count;
  const std::string tmp = std::string(path) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("FQ", tmp, file)) return false;
    if (file.write(&header, sizeof(header)) != sizeof(header)) return false;
    if (count && file.write(records, sizeof(ChapterRecord) * count) != sizeof(ChapterRecord) * count) return false;
    file.flush();
  }
  Storage.remove(path);
  return Storage.rename(tmp.c_str(), path);
}

bool chapterCount(const char* bookId, uint32_t& count) {
  count = 0;
  char dir[96];
  char path[128];
  if (!idSafe(bookId) || !bookDir(bookId, dir, sizeof(dir))) return false;
  snprintf(path, sizeof(path), "%s/chapters.bin", dir);
  ChapterHeader header;
  if (!readAll(path, &header, sizeof(header))) return false;
  if (header.magic != kChapterMagic || header.version != kChapterVersion || header.recordSize != sizeof(ChapterRecord))
    return false;
  count = header.count;
  return true;
}

bool readChapter(const char* bookId, uint32_t index, ChapterRecord& record) {
  char dir[96];
  char path[128];
  if (!bookDir(bookId, dir, sizeof(dir))) return false;
  snprintf(path, sizeof(path), "%s/chapters.bin", dir);
  HalFile file;
  if (!Storage.openFileForRead("FQ", path, file)) return false;
  ChapterHeader header;
  if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != kChapterMagic || index >= header.count)
    return false;
  if (!file.seek(sizeof(header) + static_cast<size_t>(index) * sizeof(ChapterRecord))) return false;
  return file.read(&record, sizeof(record)) == sizeof(record);
}

bool chapterTextPath(const char* bookId, uint32_t index, char* out, size_t cap) {
  const int n = snprintf(out, cap, "/.crosspoint/fanqie/books/%s/c/%05u.txt", bookId, index);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool hasChapterText(const char* bookId, uint32_t index) {
  char path[128];
  return chapterTextPath(bookId, index, path, sizeof(path)) && Storage.exists(path);
}

bool loadMeta(const char* bookId, BookMeta& meta) {
  char dir[96];
  char path[128];
  if (!bookDir(bookId, dir, sizeof(dir))) return false;
  snprintf(path, sizeof(path), "%s/meta.bin", dir);
  HalFile file;
  if (!Storage.openFileForRead("FQ", path, file)) return false;
  MetaHeader header;
  if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != kMetaMagic ||
      header.version != kMetaVersion || header.bytes != sizeof(BookMeta))
    return false;
  return file.read(&meta, sizeof(meta)) == sizeof(meta);
}

bool saveMeta(const BookMeta& meta) {
  if (!idSafe(meta.bookId) || !ensureRoot()) return false;
  char dir[96];
  char path[128];
  if (!bookDir(meta.bookId, dir, sizeof(dir)) || !Storage.mkdir(dir)) return false;
  snprintf(path, sizeof(path), "%s/meta.bin", dir);
  uint8_t buffer[sizeof(MetaHeader) + sizeof(BookMeta)];
  MetaHeader header;
  memcpy(buffer, &header, sizeof(header));
  memcpy(buffer + sizeof(header), &meta, sizeof(meta));
  if (!writeAll(path, buffer, sizeof(buffer))) return false;

  IndexHeader indexHeader;
  IndexRecord records[kMaxBooks];
  HalFile file;
  uint16_t count = 0;
  if (Storage.exists(kIndex) && Storage.openFileForRead("FQ", kIndex, file)) {
    if (file.read(&indexHeader, sizeof(indexHeader)) == sizeof(indexHeader) && indexHeader.magic == kIndexMagic &&
        indexHeader.count <= kMaxBooks) {
      count = indexHeader.count;
      if (count && file.read(records, sizeof(IndexRecord) * count) != sizeof(IndexRecord) * count) count = 0;
    }
  }
  int slot = -1;
  for (uint16_t i = 0; i < count; ++i) {
    if (strcmp(records[i].bookId, meta.bookId) == 0) slot = static_cast<int>(i);
  }
  if (slot < 0) {
    if (count >= kMaxBooks) return false;
    slot = count++;
  }
  memset(&records[slot], 0, sizeof(records[slot]));
  strncpy(records[slot].bookId, meta.bookId, sizeof(records[slot].bookId) - 1);
  strncpy(records[slot].epubPath, meta.epubPath, sizeof(records[slot].epubPath) - 1);
  uint8_t indexBuffer[sizeof(IndexHeader) + sizeof(records)];
  IndexHeader outHeader;
  outHeader.count = count;
  memcpy(indexBuffer, &outHeader, sizeof(outHeader));
  memcpy(indexBuffer + sizeof(outHeader), records, sizeof(IndexRecord) * count);
  return writeAll(kIndex, indexBuffer, sizeof(outHeader) + sizeof(IndexRecord) * count);
}

bool lookupEpubPath(const char* epubPath, BookMeta& meta) {
  HalFile file;
  if (!Storage.exists(kIndex) || !Storage.openFileForRead("FQ", kIndex, file)) return false;
  IndexHeader header;
  if (file.read(&header, sizeof(header)) != sizeof(header) || header.magic != kIndexMagic || header.count > kMaxBooks)
    return false;
  for (uint16_t i = 0; i < header.count; ++i) {
    IndexRecord record;
    if (file.read(&record, sizeof(record)) != sizeof(record)) return false;
    if (strcmp(record.epubPath, epubPath) == 0) return loadMeta(record.bookId, meta);
  }
  return false;
}

bool bookIdFromEpubPath(const char* path, char* bookId, size_t cap) {
  constexpr char kPrefix[] = "/Fanqie/";
  if (!path || strncmp(path, kPrefix, sizeof(kPrefix) - 1) != 0) return false;
  const char* name = path + sizeof(kPrefix) - 1;
  const char* dot = strrchr(name, '.');
  if (!dot || strcmp(dot, ".epub") != 0 || strchr(name, '/') != nullptr) return false;
  const size_t length = static_cast<size_t>(dot - name);
  if (length == 0 || length >= cap || length >= 24) return false;
  for (size_t i = 0; i < length; ++i) {
    if (name[i] < '0' || name[i] > '9') return false;
  }
  memcpy(bookId, name, length);
  bookId[length] = '\0';
  return true;
}

bool adoptEpubFile(const char* path) {
  char bookId[24];
  if (!bookIdFromEpubPath(path, bookId, sizeof(bookId)) || !Storage.exists(path)) return false;
  BookMeta existing;
  if (loadMeta(bookId, existing) && strcmp(existing.epubPath, path) == 0) return true;
  BookMeta meta;
  snprintf(meta.bookId, sizeof(meta.bookId), "%s", bookId);
  snprintf(meta.title, sizeof(meta.title), "%s", bookId);
  snprintf(meta.epubPath, sizeof(meta.epubPath), "%s", path);
  return saveMeta(meta);
}

bool adoptCachedBooks() {
  bool adopted = false;
  const auto names = Storage.listFiles("/Fanqie", kMaxBooks);
  for (const auto& name : names) {
    char path[128];
    const int written = snprintf(path, sizeof(path), "/Fanqie/%s", name.c_str());
    if (written > 0 && static_cast<size_t>(written) < sizeof(path) && adoptEpubFile(path)) adopted = true;
  }
  return adopted;
}

bool findByEpubPath(const char* epubPath, BookMeta& meta) {
  if (!epubPath || !epubPath[0]) return false;
  if (lookupEpubPath(epubPath, meta)) return true;
  adoptCachedBooks();
  adoptEpubFile(epubPath);
  return lookupEpubPath(epubPath, meta);
}

bool epubPathFor(const char* bookId, char* out, size_t cap) {
  if (!idSafe(bookId)) return false;
  const int n = snprintf(out, cap, "/Fanqie/%s.epub", bookId);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool loadContentKey(char* hex, size_t cap) {
  if (!hex || cap < 33) return false;
  HalFile file;
  if (!Storage.openFileForRead("FQ", kContentKey, file)) return false;
  const size_t n = file.read(hex, cap - 1);
  hex[n] = '\0';
  return n == 32;
}

bool saveContentKey(const char* hex) {
  if (!hex || strlen(hex) != 32 || !ensureRoot()) return false;
  return writeAll(kContentKey, hex, 32);
}

bool loadDeviceId(char* out, size_t cap) {
  if (!out || cap < 20) return false;
  HalFile file;
  if (!Storage.openFileForRead("FQ", kDevice, file)) return false;
  const size_t n = file.read(out, cap - 1);
  out[n] = '\0';
  return n == 19;
}

bool saveDeviceId(const char* id) {
  if (!id || strlen(id) != 19 || !ensureRoot()) return false;
  return writeAll(kDevice, id, 19);
}

}  // namespace FanqieStore

#endif
