#pragma once

#include <string>

#include "activities/Activity.h"

class FanqieSyncActivity final : public Activity {
 public:
  enum class Mode : uint8_t { Open, Upload, Shelf };

  FanqieSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path, Mode mode,
                     HomeMenuItem homeItem = HomeMenuItem::NONE)
      : Activity("FanqieSync", renderer, mappedInput), path_(std::move(path)), mode_(mode), homeItem_(homeItem) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return phase_ == Phase::Wifi || phase_ == Phase::Work; }

 private:
  enum class Phase : uint8_t { Wifi, Work, Offline, Done };

  std::string path_;
  Mode mode_ = Mode::Open;
  HomeMenuItem homeItem_ = HomeMenuItem::NONE;
  Phase phase_ = Phase::Wifi;
  bool worked_ = false;
  char message_[96] = {};

  void launchWifi();
  void showOffline();
  void runWork();
  void leave();
};
