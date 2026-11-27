#pragma once

#include <cstdint>
#include <memory>

#include "FanqieProtocol.h"
#include "FanqieStore.h"
#include "activities/Activity.h"

class FanqieActivity final : public Activity {
 public:
  FanqieActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("Fanqie", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class Screen : uint8_t { Disclaimer, Accounts, Qr, Message, ConfirmLogout };
  enum class OnlineAction : uint8_t { None, Login, Shelf };

  Screen screen_ = Screen::Disclaimer;
  int selected_ = 0;
  bool confirmArmed_ = false;
  bool accepted_ = false;
  bool radioUsed_ = false;
  OnlineAction online_ = OnlineAction::None;
  uint32_t nextPollAt_ = 0;
  char token_[96] = {};
  char message_[128] = {};
  char nicknames_[FanqieStore::kMaxAccounts][FanqieStore::kNickname] = {};
  int accountCount_ = 0;
  int activeAccount_ = -1;
  FanqieProtocol::CookieJar jar_;
  std::unique_ptr<uint8_t[]> qr_;
  size_t qrLen_ = 0;
  int qrWidth_ = 0;
  int qrHeight_ = 0;

  int itemCount() const;
  void reloadAccounts();
  void activate();
  void beginOnline(OnlineAction action);
  void runOnline();
  void showOffline();
  void showFailure(FanqieSession::Status status, const FanqieSession::Failure& failure);
  void startQr();
  void pollQr();
  void drawQr(const Rect& safe) const;
};
