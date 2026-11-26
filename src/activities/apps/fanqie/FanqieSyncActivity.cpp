#include "FanqieSyncActivity.h"

#if !FANQIE_NOVEL

// Fanqie sync runs only in the X4 Pro firmware.

#else

#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstring>

#include "FanqieNet.h"
#include "FanqieSession.h"
#include "NetworkStartup.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

void FanqieSyncActivity::onEnter() {
  Activity::onEnter();
  const char* text = tr(STR_FANQIE_OPENING);
  if (mode_ == Mode::Upload) text = tr(STR_FANQIE_UPLOADING);
  if (mode_ == Mode::Shelf) text = tr(STR_FANQIE_SYNCING);
  snprintf(message_, sizeof(message_), "%s", text);
  if (!FanqieNet::connected()) beginWifi();
  phase_ = Phase::Connect;
  connectStartedAt_ = millis();
  requestUpdate();
}

void FanqieSyncActivity::onExit() {
  stopWifi();
  Activity::onExit();
}

bool FanqieSyncActivity::beginWifi() {
  if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();
  const auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
  if (!credential) return false;
  NetworkStartup::prepare(renderer);
  if (!NetworkStartup::setMode(renderer, WIFI_STA)) return false;
  WiFi.setSleep(false);
  if (credential->password.empty()) WiFi.begin(credential->ssid.c_str());
  else WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  ownsWifi_ = true;
  return true;
}

void FanqieSyncActivity::stopWifi() {
  if (!ownsWifi_) return;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  ownsWifi_ = false;
}

void FanqieSyncActivity::runWork() {
  FanqieSession::Status status = FanqieSession::Status::Failed;
  if (mode_ == Mode::Shelf) status = FanqieSession::syncShelf();
  else if (mode_ == Mode::Upload) status = FanqieSession::uploadProgress(path_.c_str());
  else status = FanqieSession::prepareOpen(path_.c_str());
  if (status == FanqieSession::Status::Offline) snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_OFFLINE));
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
  if (phase_ == Phase::Connect) {
    if (!ownsWifi_ || FanqieNet::connected() || millis() - connectStartedAt_ > 12000) phase_ = Phase::Work;
    else return;
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
