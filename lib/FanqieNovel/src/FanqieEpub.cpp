#include "FanqieEpub.h"

#if !FANQIE_NOVEL

namespace fanqie_internal {
constexpr int kEpubOmitted = 0;
}

#else

#include "FanqieStore.h"

#include "../../miniz/src/MinizConfig.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace FanqieEpub {
namespace {

uint32_t crc32(const uint8_t* data, size_t len) { return static_cast<uint32_t>(mz_crc32(0, data, len)); }

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

void xmlEscape(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (const char* p = in ? in : ""; *p && o + 6 < cap; ++p) {
    const char* rep = nullptr;
    if (*p == '&') rep = "&amp;";
    else if (*p == '<') rep = "&lt;";
    else if (*p == '>') rep = "&gt;";
    if (rep) {
      const size_t n = strlen(rep);
      memcpy(out + o, rep, n);
      o += n;
    } else {
      out[o++] = *p;
    }
  }
  out[o] = '\0';
}

class ZipWriter {
 public:
  bool begin(const char* path) {
    final_ = path;
    temp_ = std::string(path) + ".tmp";
    centralPath_ = std::string(path) + ".central";
    if (!Storage.openFileForWrite("FQ", temp_, zip_)) return false;
    if (!Storage.openFileForWrite("FQ", centralPath_, central_)) return false;
    return true;
  }

  bool add(const char* name, const uint8_t* data, size_t len) {
    uint8_t local[30];
    memset(local, 0, sizeof(local));
    put32(local, 0x04034b50);
    put16(local + 4, 20);
    put32(local + 14, crc32(data, len));
    put32(local + 18, static_cast<uint32_t>(len));
    put32(local + 22, static_cast<uint32_t>(len));
    put16(local + 26, static_cast<uint16_t>(strlen(name)));
    if (zip_.write(local, sizeof(local)) != sizeof(local) || zip_.write(name, strlen(name)) != strlen(name) ||
        (len && zip_.write(data, len) != len))
      return false;
    uint8_t central[46];
    memset(central, 0, sizeof(central));
    put32(central, 0x02014b50);
    put16(central + 4, 20);
    put16(central + 6, 20);
    put32(central + 16, crc32(data, len));
    put32(central + 20, static_cast<uint32_t>(len));
    put32(central + 24, static_cast<uint32_t>(len));
    put16(central + 28, static_cast<uint16_t>(strlen(name)));
    put32(central + 42, offset_);
    if (central_.write(central, sizeof(central)) != sizeof(central) ||
        central_.write(name, strlen(name)) != strlen(name))
      return false;
    offset_ += static_cast<uint32_t>(sizeof(local) + strlen(name) + len);
    centralBytes_ += static_cast<uint32_t>(sizeof(central) + strlen(name));
    ++count_;
    return true;
  }

  bool finish() {
    zip_.flush();
    central_.flush();
    central_ = HalFile{};
    HalFile centralRead;
    if (!Storage.openFileForRead("FQ", centralPath_, centralRead)) return false;
    uint8_t buffer[256];
    while (true) {
      const size_t n = centralRead.read(buffer, sizeof(buffer));
      if (n == 0) break;
      if (zip_.write(buffer, n) != n) return false;
    }
    uint8_t end[22];
    memset(end, 0, sizeof(end));
    put32(end, 0x06054b50);
    put16(end + 8, count_);
    put16(end + 10, count_);
    put32(end + 12, centralBytes_);
    put32(end + 16, offset_);
    if (zip_.write(end, sizeof(end)) != sizeof(end)) return false;
    zip_.flush();
    zip_ = HalFile{};
    Storage.remove(final_.c_str());
    const bool renamed = Storage.rename(temp_.c_str(), final_.c_str());
    Storage.remove(centralPath_.c_str());
    return renamed;
  }

  void abort() {
    zip_ = HalFile{};
    central_ = HalFile{};
    Storage.remove(temp_.c_str());
    Storage.remove(centralPath_.c_str());
  }

 private:
  HalFile zip_;
  HalFile central_;
  std::string final_;
  std::string temp_;
  std::string centralPath_;
  uint32_t offset_ = 0;
  uint32_t centralBytes_ = 0;
  uint16_t count_ = 0;
};

constexpr char kPlaceholder[] = "This chapter is not cached on the device yet.";

}  // namespace

bool build(const char* bookId) {
  FanqieStore::BookMeta meta;
  if (!FanqieStore::loadMeta(bookId, meta)) return false;
  uint32_t count = 0;
  if (!FanqieStore::chapterCount(bookId, count)) return false;
  if (!Storage.mkdir("/Fanqie")) return false;
  ZipWriter zip;
  if (!zip.begin(meta.epubPath)) return false;
  const uint8_t mime[] = {'a', 'p', 'p', 'l', 'i', 'c', 'a', 't', 'i', 'o', 'n', '/', 'e', 'p', 'u', 'b', '+', 'z', 'i',
                          'p'};
  if (!zip.add("mimetype", mime, sizeof(mime))) {
    zip.abort();
    return false;
  }
  const char* container =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
      "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>";
  if (!zip.add("META-INF/container.xml", reinterpret_cast<const uint8_t*>(container), strlen(container))) {
    zip.abort();
    return false;
  }

  // The package document lists every chapter. It is written after the chapters
  // only if it fits; build it first into a heap buffer sized from the count.
  const size_t opfCap = 512 + static_cast<size_t>(count) * 180;
  auto opf = makeUniqueNoThrow<char[]>(opfCap);
  if (!opf) {
    LOG_ERR("FQ", "OOM: epub package %u", static_cast<unsigned>(opfCap));
    zip.abort();
    return false;
  }
  char title[160];
  char author[120];
  xmlEscape(meta.title[0] ? meta.title : meta.bookId, title, sizeof(title));
  xmlEscape(meta.author, author, sizeof(author));
  int used = snprintf(opf.get(), opfCap,
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<package xmlns=\"http://www.idpf.org/2007/opf\" unique-identifier=\"bid\" version=\"2.0\">"
                      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                      "<dc:title>%s</dc:title><dc:creator>%s</dc:creator>"
                      "<dc:language>zh</dc:language><dc:identifier id=\"bid\">fanqie-%s</dc:identifier>"
                      "</metadata><manifest>",
                      title, author, meta.bookId);
  if (used < 0 || static_cast<size_t>(used) >= opfCap) {
    zip.abort();
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const int n = snprintf(opf.get() + used, opfCap - static_cast<size_t>(used),
                           "<item id=\"c%u\" href=\"chapter%u.xhtml\" media-type=\"application/xhtml+xml\"/>", i, i);
    if (n < 0 || static_cast<size_t>(used + n) >= opfCap) {
      zip.abort();
      return false;
    }
    used += n;
  }
  int n = snprintf(opf.get() + used, opfCap - static_cast<size_t>(used), "</manifest><spine>");
  if (n < 0 || static_cast<size_t>(used + n) >= opfCap) {
    zip.abort();
    return false;
  }
  used += n;
  for (uint32_t i = 0; i < count; ++i) {
    n = snprintf(opf.get() + used, opfCap - static_cast<size_t>(used), "<itemref idref=\"c%u\"/>", i);
    if (n < 0 || static_cast<size_t>(used + n) >= opfCap) {
      zip.abort();
      return false;
    }
    used += n;
  }
  n = snprintf(opf.get() + used, opfCap - static_cast<size_t>(used), "</spine></package>");
  if (n < 0 || static_cast<size_t>(used + n) >= opfCap) {
    zip.abort();
    return false;
  }
  used += n;

  constexpr size_t kPageCap = 56 * 1024;
  auto page = makeUniqueNoThrow<char[]>(kPageCap);
  auto raw = makeUniqueNoThrow<char[]>(48 * 1024);
  if (!page || !raw) {
    LOG_ERR("FQ", "OOM: epub page");
    zip.abort();
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    FanqieStore::ChapterRecord chapter;
    if (!FanqieStore::readChapter(bookId, i, chapter)) {
      zip.abort();
      return false;
    }
    char heading[160];
    xmlEscape(chapter.title[0] ? chapter.title : "Chapter", heading, sizeof(heading));
    const char* body = kPlaceholder;
    char textPath[128];
    if (FanqieStore::chapterTextPath(bookId, i, textPath, sizeof(textPath)) && Storage.exists(textPath)) {
      HalFile text;
      if (Storage.openFileForRead("FQ", textPath, text)) {
        const size_t got = text.read(raw.get(), 48 * 1024 - 1);
        if (got > 0) {
          raw[got] = '\0';
          body = raw.get();
        }
      }
    }
    const int head = snprintf(page.get(), kPageCap,
                              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                              "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>%s</title></head>"
                              "<body><h1>%s</h1><p>",
                              heading, heading);
    if (head < 0 || static_cast<size_t>(head) >= kPageCap) {
      zip.abort();
      return false;
    }
    char* cursor = page.get() + head;
    xmlEscape(body, cursor, kPageCap - static_cast<size_t>(head));
    const size_t bodyLen = strlen(cursor);
    if (head + bodyLen + 32 >= kPageCap) {
      zip.abort();
      return false;
    }
    constexpr char kTail[] = "</p></body></html>";
    memcpy(cursor + bodyLen, kTail, sizeof(kTail) - 1);
    const size_t full = static_cast<size_t>(head) + bodyLen + sizeof(kTail) - 1;
    char name[32];
    snprintf(name, sizeof(name), "OEBPS/chapter%u.xhtml", i);
    if (!zip.add(name, reinterpret_cast<const uint8_t*>(page.get()), full)) {
      zip.abort();
      return false;
    }
  }
  if (!zip.add("OEBPS/content.opf", reinterpret_cast<const uint8_t*>(opf.get()), static_cast<size_t>(used))) {
    zip.abort();
    return false;
  }
  if (!zip.finish()) {
    zip.abort();
    return false;
  }
  return true;
}

}  // namespace FanqieEpub

#endif
