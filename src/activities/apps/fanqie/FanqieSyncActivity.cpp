#include "FanqieSyncActivity.h"

#if !FANQIE_NOVEL

// Fanqie sync runs only in the X4 Pro firmware.

#else

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "FanqieNet.h"
#include "FanqieSession.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FanqieSyncActivity::onEnter() {
  Activity::onEnter();
  const char* text = tr(STR_FANQIE_OPENING);
  if (mode_ == Mode::Upload) text = tr(STR_FANQIE_UPLOADING);
  if (mode_ == Mode::Shelf) text = tr(STR_FANQIE_SYNCING);
  snprintf(message_, sizeof(message_), "%s", text);
  if (FanqieNet::connected())
    phase_ = Phase::Work;
  else
    launchWifi();
  requestUpdate();
}

void FanqieSyncActivity::onExit() {
  FanqieNet::releaseRadio();
  Activity::onExit();
}

void FanqieSyncActivity::showOffline() {
  snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_OFFLINE));
  phase_ = Phase::Offline;
  FanqieNet::releaseRadio();
  requestUpdate();
}

void FanqieSyncActivity::launchWifi() {
  phase_ = Phase::Wifi;
  // WifiSelectionActivity tries the last saved network, then the system list.
  if (!startActivityForResultWith<WifiSelectionActivity>([this](const ActivityResult& result) {
        if (result.isCancelled || !FanqieNet::connected()) {
          showOffline();
          return;
        }
        phase_ = Phase::Work;
        requestUpdate();
      })) {
    LOG_ERR("FQ", "OOM: wifi selection");
    showOffline();
  }
}

void FanqieSyncActivity::runWork() {
  FanqieSession::Status status = FanqieSession::Status::Failed;
  if (mode_ == Mode::Shelf)
    status = FanqieSession::syncShelf();
  else if (mode_ == Mode::Upload)
    status = FanqieSession::uploadProgress(path_.c_str());
  else
    status = FanqieSession::prepareOpen(path_.c_str());
  if (status == FanqieSession::Status::Offline)
    snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_OFFLINE));
  else if (status == FanqieSession::Status::NeedAccount)
    snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_NO_ACCOUNT));
  else if (status == FanqieSession::Status::Failed)
    snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_CHAPTER_FAILED));
  else if (mode_ == Mode::Upload && status == FanqieSession::Status::Offline)
    snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_PROGRESS_QUEUED));
}

void FanqieSyncActivity::leave() {
  if (mode_ == Mode::Open) {
    activityManager.goToReader(std::move(path_), false, true);
    return;
  }
  if (mode_ == Mode::Upload) {
    activityManager.goHome(homeItem_);
    return;
  }
  finish();
}

void FanqieSyncActivity::loop() {
  if (phase_ == Phase::Wifi) return;
  if (phase_ == Phase::Offline) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTouchReleased()) leave();
    return;
  }
  if (phase_ == Phase::Work && !worked_) {
    worked_ = true;
    requestUpdateAndWait();
    runWork();
    phase_ = Phase::Done;
    leave();
  }
}

void FanqieSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y, safe.width, metrics.headerHeight}, tr(STR_FANQIE_TITLE));
  renderer.drawText(UI_12_FONT_ID, safe.x, safe.y + metrics.headerHeight + 16, message_, true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif
