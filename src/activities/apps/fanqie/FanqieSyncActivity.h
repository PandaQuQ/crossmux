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
  bool preventAutoSleep() override { return phase_ != Phase::Done; }

 private:
  enum class Phase : uint8_t { Connect, Work, Done };

  std::string path_;
  Mode mode_ = Mode::Open;
  HomeMenuItem homeItem_ = HomeMenuItem::NONE;
  Phase phase_ = Phase::Connect;
  bool ownsWifi_ = false;
  bool worked_ = false;
  uint32_t connectStartedAt_ = 0;
  char message_[96] = {};

  bool beginWifi();
  void stopWifi();
  void runWork();
  void leave();
};
