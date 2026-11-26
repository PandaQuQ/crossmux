#include <HalStorage.h>
#include <HardwareSerial.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "FanqieEpub.h"
#include "FanqieStore.h"

HardwareSerial Serial;

namespace {

class FanqieStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() / "crossmux-fanqie-store";
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    ASSERT_TRUE(std::filesystem::create_directories(root_));
    ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", root_.c_str(), 1), 0);
    ASSERT_TRUE(Storage.begin());
  }

  void TearDown() override {
    unsetenv("CROSSPOINT_SIM_SD");
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path root_;
};

}  // namespace

TEST_F(FanqieStoreTest, AdoptsEpubWhenIndexIsMissing) {
  ASSERT_TRUE(Storage.mkdir("/Fanqie"));
  {
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("FQ", "/Fanqie/7000000000000000001.epub", file));
    const char mark[] = "epub";
    ASSERT_EQ(file.write(mark, sizeof(mark) - 1), sizeof(mark) - 1);
  }
  FanqieStore::BookMeta found;
  ASSERT_TRUE(FanqieStore::findByEpubPath("/Fanqie/7000000000000000001.epub", found));
  EXPECT_STREQ(found.bookId, "7000000000000000001");
  EXPECT_STREQ(found.epubPath, "/Fanqie/7000000000000000001.epub");
  EXPECT_FALSE(FanqieStore::findByEpubPath("/Fanqie/not-a-book.epub", found));
}

TEST_F(FanqieStoreTest, AccountsChaptersMetaAndEpubRoundTrip) {
  FanqieStore::Account accounts[2] = {};
  std::snprintf(accounts[0].nickname, sizeof(accounts[0].nickname), "Ada");
  std::snprintf(accounts[0].cookie, sizeof(accounts[0].cookie), "sessionid=one");
  std::snprintf(accounts[1].nickname, sizeof(accounts[1].nickname), "Bea");
  std::snprintf(accounts[1].cookie, sizeof(accounts[1].cookie), "sessionid=two");
  ASSERT_TRUE(FanqieStore::saveAccounts(accounts, 2, 1));

  FanqieStore::Account loaded[2] = {};
  int count = 0;
  int active = -1;
  ASSERT_TRUE(FanqieStore::loadAccounts(loaded, count, active));
  EXPECT_EQ(count, 2);
  EXPECT_EQ(active, 1);
  EXPECT_STREQ(loaded[1].cookie, "sessionid=two");
  FanqieStore::Account current;
  ASSERT_TRUE(FanqieStore::loadActiveAccount(current));
  EXPECT_STREQ(current.nickname, "Bea");

  FanqieStore::ChapterRecord chapters[2] = {};
  std::snprintf(chapters[0].itemId, sizeof(chapters[0].itemId), "100");
  std::snprintf(chapters[0].title, sizeof(chapters[0].title), "One");
  std::snprintf(chapters[1].itemId, sizeof(chapters[1].itemId), "101");
  std::snprintf(chapters[1].title, sizeof(chapters[1].title), "Two & More");
  ASSERT_TRUE(FanqieStore::saveChapters("7000000000000000001", chapters, 2));
  char textPath[128];
  ASSERT_TRUE(FanqieStore::chapterTextPath("7000000000000000001", 0, textPath, sizeof(textPath)));
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/fanqie/books/7000000000000000001/c"));
  {
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("FQ", textPath, file));
    const char* text = "Hello cached chapter.";
    ASSERT_EQ(file.write(text, strlen(text)), strlen(text));
  }

  FanqieStore::BookMeta meta;
  std::snprintf(meta.bookId, sizeof(meta.bookId), "7000000000000000001");
  std::snprintf(meta.title, sizeof(meta.title), "Fixture");
  std::snprintf(meta.author, sizeof(meta.author), "CrossMux");
  ASSERT_TRUE(FanqieStore::epubPathFor(meta.bookId, meta.epubPath, sizeof(meta.epubPath)));
  meta.chapterCount = 2;
  meta.dirty = 1;
  meta.localUnix = 10;
  meta.cloudUnix = 20;
  ASSERT_TRUE(FanqieStore::saveMeta(meta));
  FanqieStore::BookMeta found;
  ASSERT_TRUE(FanqieStore::findByEpubPath(meta.epubPath, found));
  EXPECT_STREQ(found.title, "Fixture");
  EXPECT_EQ(found.dirty, 1);

  ASSERT_TRUE(FanqieEpub::build(meta.bookId));
  const auto epub = root_ / "Fanqie" / "7000000000000000001.epub";
  ASSERT_TRUE(std::filesystem::exists(epub));
  std::ifstream input(epub, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_EQ(bytes.compare(0, 2, "PK"), 0);
  EXPECT_NE(bytes.find("application/epub+zip"), std::string::npos);
  EXPECT_NE(bytes.find("mimetype"), std::string::npos);
  EXPECT_NE(bytes.find("Hello cached chapter."), std::string::npos);
  EXPECT_NE(bytes.find("Two &amp; More"), std::string::npos);
  EXPECT_NE(bytes.find("This chapter is not cached"), std::string::npos);
  EXPECT_NE(bytes.find("content.opf"), std::string::npos);
}
