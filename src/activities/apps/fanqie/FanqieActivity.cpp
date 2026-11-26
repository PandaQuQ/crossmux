#include "FanqieActivity.h"

#if !FANQIE_NOVEL

// Fanqie is compiled only for the X4 Pro image.

#else

#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <PNGdec.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "FanqieSession.h"
#include "FanqieStore.h"
#include "activities/apps/fanqie/FanqieSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr char kAccepted[] = "/.crosspoint/fanqie/accepted";

struct QrBlit {
  GfxRenderer* renderer = nullptr;
  int originX = 0;
  int originY = 0;
  int scale = 1;
};

int blitQr(PNGDRAW* draw) {
  auto* blit = static_cast<QrBlit*>(draw->pUser);
  if (!blit || !blit->renderer || !draw->pPixels) return 1;
  const int y = blit->originY + draw->y * blit->scale;
  for (int x = 0; x < draw->iWidth; ++x) {
    bool black = false;
    if (draw->iBpp == 1) black = (draw->pPixels[x >> 3] & (0x80 >> (x & 7))) != 0;
    else if (draw->iBpp >= 8) black = draw->pPixels[x] < 128;
    else black = (draw->pPixels[x >> 1] & (x & 1 ? 0x0f : 0xf0)) < 8;
    if (!black) continue;
    for (int sy = 0; sy < blit->scale; ++sy) {
      for (int sx = 0; sx < blit->scale; ++sx) {
        blit->renderer->drawPixel(blit->originX + x * blit->scale + sx, y + sy, true);
      }
    }
  }
  return 1;
}

}  // namespace

void FanqieActivity::onEnter() {
  Activity::onEnter();
  confirmArmed_ = !mappedInput.isPressed(MappedInputManager::Button::Confirm);
  accepted_ = Storage.exists(kAccepted);
  screen_ = accepted_ ? Screen::Accounts : Screen::Disclaimer;
  selected_ = 0;
  if (!FanqieStore::ensureRoot()) LOG_ERR("FQ", "fanqie storage unavailable");
  requestUpdate();
}

void FanqieActivity::onExit() {
  qr_.reset();
  qrLen_ = 0;
  jar_.clear();
  Activity::onExit();
}

bool FanqieActivity::preventAutoSleep() { return screen_ == Screen::Qr; }

int FanqieActivity::itemCount() const {
  FanqieStore::Account accounts[FanqieStore::kMaxAccounts];
  int count = 0;
  int active = -1;
  FanqieStore::loadAccounts(accounts, count, active);
  return count + 2 + (count > 0 ? 1 : 0);
}

void FanqieActivity::startQr() {
  qr_ = makeUniqueNoThrow<uint8_t[]>(16 * 1024);
  if (!qr_) {
    LOG_ERR("FQ", "OOM: login qr");
    screen_ = Screen::Message;
    snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_LOGIN_FAILED));
    return;
  }
  const auto status = FanqieSession::startLogin(jar_, token_, sizeof(token_), qr_.get(), 16 * 1024, qrLen_);
  if (status == FanqieSession::Status::Offline) {
    screen_ = Screen::Message;
    snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_OFFLINE));
    return;
  }
  if (status != FanqieSession::Status::Ok) {
    screen_ = Screen::Message;
    snprintf(message_, sizeof(message_), "%s", tr(STR_FANQIE_LOGIN_FAILED));
    return;
  }
  screen_ = Screen::Qr;
  nextPollAt_ = millis() + 3000;
}

void FanqieActivity::pollQr() {
  const auto status = FanqieSession::pollLogin(jar_, token_);
  if (status == FanqieSession::Status::Waiting) {
    nextPollAt_ = millis() + 3000;
    return;
  }
  if (status == FanqieSession::Status::Ok && FanqieSession::storeAccount(jar_)) {
    screen_ = Screen::Accounts;
    jar_.clear();
    qr_.reset();
    return;
  }
  screen_ = Screen::Message;
  snprintf(message_, sizeof(message_), "%s",
           status == FanqieSession::Status::Expired ? tr(STR_FANQIE_LOGIN_EXPIRED) : tr(STR_FANQIE_LOGIN_FAILED));
}

void FanqieActivity::activate() {
  if (screen_ == Screen::Disclaimer) {
    HalFile file;
    if (Storage.openFileForWrite("FQ", kAccepted, file)) {
      const char mark = '1';
      file.write(&mark, 1);
    }
    accepted_ = true;
    screen_ = Screen::Accounts;
    return;
  }
  if (screen_ == Screen::Message) {
    screen_ = Screen::Accounts;
    return;
  }
  if (screen_ == Screen::ConfirmLogout) {
    if (selected_ == 0) {
      FanqieStore::Account accounts[FanqieStore::kMaxAccounts];
      int count = 0;
      int active = -1;
      if (FanqieStore::loadAccounts(accounts, count, active) && active >= 0 && active < count) {
        for (int i = active; i + 1 < count; ++i) accounts[i] = accounts[i + 1];
        --count;
        FanqieStore::saveAccounts(accounts, count, count > 0 ? 0 : -1);
      }
    }
    screen_ = Screen::Accounts;
    selected_ = 0;
    return;
  }
  if (screen_ != Screen::Accounts) return;
  FanqieStore::Account accounts[FanqieStore::kMaxAccounts];
  int count = 0;
  int active = -1;
  FanqieStore::loadAccounts(accounts, count, active);
  if (selected_ < count) {
    FanqieStore::saveAccounts(accounts, count, selected_);
    requestUpdate();
    return;
  }
  if (selected_ == count) {
    startQr();
    requestUpdate();
    return;
  }
  if (selected_ == count + 1) {
    startActivityForResultWith<FanqieSyncActivity>([](const ActivityResult&) {}, std::string(),
                                                   FanqieSyncActivity::Mode::Shelf);
    return;
  }
  if (count > 0 && selected_ == count + 2) {
    screen_ = Screen::ConfirmLogout;
    selected_ = 1;
    requestUpdate();
  }
}

void FanqieActivity::loop() {
  if (!confirmArmed_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) confirmArmed_ = true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (screen_ == Screen::Accounts || screen_ == Screen::Disclaimer) activityManager.goHome();
    else screen_ = Screen::Accounts;
    requestUpdate();
    return;
  }
  if (screen_ == Screen::Qr && millis() >= nextPollAt_) {
    pollQr();
    requestUpdate();
  }
  const int count = screen_ == Screen::Accounts ? itemCount() : screen_ == Screen::ConfirmLogout ? 2 : 1;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && selected_ > 0) {
    --selected_;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) && selected_ + 1 < count) {
    ++selected_;
    requestUpdate();
  }
  if (screen_ == Screen::Accounts) {
    const auto touch = handleListTouch(selected_, count, 80, renderer.getScreenHeight() - 140, false);
    if (touch == ListTouchResult::Activated && confirmArmed_) activate();
  }
  if (confirmArmed_ && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activate();
}

void FanqieActivity::drawQr(const Rect& safe) const {
  if (!qr_ || qrLen_ < 8) return;
  PNG png;
  auto bytes = const_cast<uint8_t*>(qr_.get());
  if (png.openRAM(bytes, static_cast<int>(qrLen_), blitQr) != 0) return;
  const int width = png.getWidth();
  const int height = png.getHeight();
  const int fit = std::min(safe.width, safe.height) - 80;
  const int scale = std::max(1, std::min(width > 0 ? fit / width : 1, height > 0 ? fit / height : 1));
  QrBlit blit{&renderer, safe.x + (safe.width - width * scale) / 2, safe.y + 48, scale};
  png.decode(&blit, 0);
  png.close();
}

void FanqieActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y, safe.width, metrics.headerHeight}, tr(STR_FANQIE_TITLE));
  const int y0 = safe.y + metrics.headerHeight + metrics.verticalSpacing;
  if (screen_ == Screen::Disclaimer) {
    renderer.drawText(UI_12_FONT_ID, safe.x, y0, tr(STR_FANQIE_DISCLAIMER), true);
  } else if (screen_ == Screen::Qr) {
    drawQr(safe);
    renderer.drawText(UI_10_FONT_ID, safe.x, safe.y + safe.height - 36, tr(STR_FANQIE_QR_HINT), true);
  } else if (screen_ == Screen::Message || screen_ == Screen::ConfirmLogout) {
    renderer.drawText(UI_12_FONT_ID, safe.x, y0,
                      screen_ == Screen::ConfirmLogout ? tr(STR_FANQIE_LOGOUT_CONFIRM) : message_, true);
  } else {
    FanqieStore::Account accounts[FanqieStore::kMaxAccounts];
    int count = 0;
    int active = -1;
    FanqieStore::loadAccounts(accounts, count, active);
    int row = 0;
    auto line = [&](const char* text) {
      const bool selected = row == selected_;
      if (selected) renderer.fillRect(safe.x, y0 + row * 28, safe.width, 26, true);
      renderer.drawText(UI_12_FONT_ID, safe.x + 4, y0 + row * 28 + 4, text, !selected);
      ++row;
    };
    for (int i = 0; i < count; ++i) {
      char label[64];
      snprintf(label, sizeof(label), "%s%s", accounts[i].nickname, i == active ? " *" : "");
      line(label);
    }
    line(tr(STR_FANQIE_ADD_ACCOUNT));
    line(tr(STR_FANQIE_SYNC_SHELF));
    if (count > 0) line(tr(STR_FANQIE_LOGOUT));
  }
  GUI.drawButtonHints(renderer, tr(STR_BACK), tr(STR_SELECT), "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

#endif
