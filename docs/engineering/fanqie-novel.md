# Fanqie Novel on X4 Pro

Fanqie (番茄小说) is an on-demand reading connector for the Xteink X4 Pro
(`pio run -e x4pro`, and the stable/nightly envs that extend `x4pro_hardware`).
The build flag is `FANQIE_NOVEL`. Other environments compile without the
connector, so the shared ESP32-C3 image does not gain this code.

This is an unofficial client. It is not affiliated with Fanqie or its operator.
It reads the signed-in user's own shelf and caches chapters for the existing
EPUB reader. It does not add a second reader or a second bookshelf.

## What is implemented

- Account login uses the official web passport
  `https://fanqienovel.com/passport/web/get_qrcode/` and
  `check_qrconnect/`, with `aid=2503`. The response `qrcode` field is a base64
  PNG drawn on the panel. The phone app that the passport names (Fanqie, or the
  official login app it currently asks for) scans that image. Cookies, including
  `sessionid`, stay in `/.crosspoint/fanqie/accounts.bin` (version 1). Up to
  four accounts can be stored, switched, and removed. Cookie values are not
  written to the log.
- Shelf ids come from
  `/reading/bookapi/bookshelf/info/v:version/`. Titles come from
  `/api/bookshelf/multidetail`. The directory comes from
  `/api/reader/directory/detail`.
- Each selected book is written to `/Fanqie/<bookId>.epub` and registered with
  `RECENT_BOOKS` plus `ReadingStatsStore::noteExternalProgress`. Home reads that
  percent. Opening the file uses `ActivityManager::goToReader` and the existing
  EPUB reader. Chapters that are not on the card yet are short placeholder
  pages, so the spine index does not move when a later chapter is filled in.
- Progress pull and push use `/api/reader/book/progress` and
  `/api/reader/book/update_progress`. Opening a Fanqie EPUB pulls when Wi-Fi is
  up. Leaving the reader uploads when Wi-Fi is up. Otherwise the position is
  marked dirty in `meta.bin` and is sent on the next online open. The chapter
  id (`item_id`) is the position key. `read_progress` values above 1 and at most
  10000 are treated as a whole-book 万分比; values from 0 to 1 are treated as a
  fraction inside the chapter, which is also what this client uploads.

## Chapter text

The web chapter URL `/api/reader/chapter/content` is tried first with the
account cookie. Public reports (KOReader fanqie plugin, late 2025/2026) say
that URL often returns HTTP 515 or an empty body, and that the anonymous web
reader locks chapters after a short preview. Text that still contains the
private-use-area font mapping is rejected instead of being shown as the wrong
characters. There is no bundled font map: the map changes and a wrong table
would silently corrupt the book.

If the web body is not usable text, the client calls the app gateway
`https://reading.snssdk.com/reading/reader/full/v/` with `aid=1967`,
`device_type=Honor10`, `os_version=0`, `version_code=66.9`, and
`novel_text_type=0`. The request carries `x-argus`, `x-ladon`, `x-khronos`,
and `x-ss-req-ticket`, matching the algorithm published in the 2025 browser
userscript that reads the same gateway (SM3, Simon, AES-128-CBC, protobuf).
Host tests lock one deterministic signature vector from that reference.
The body is base64(IV || AES-128-CBC), then gzip when the plaintext starts
with a gzip header. The content key comes from `/reading/crypt/registerkey`
and is decrypted with the published master key. A 19-digit device id starting
with `7` is generated locally. Full `device_register` / `ttEncrypt` is not
ported.

The `privilege/add` call used by some downloaders is intentionally absent. It
is not required to parse the user's own shelf, and it is not a progress or
login API.

## Limits

- A direct request to `get_qrcode` with `aid=2503` returned HTTP 200 and a
  JSON body whose `data.qrcode` is raw PNG base64 (about 2.6KB) and whose
  `message` is `success`. The firmware does not send `Accept-Encoding`, and
  the same request without it is plain JSON. The X4 Pro image uses wolfSSL
  with `setInsecure()`, so that build does not check the passport certificate
  and does not need SNTP for this call. The login request previously never
  left the device: `WeReadHttpClient` rejects a call that has no read buffer,
  and Fanqie did not provide one, so the screen said login failed as soon as
  Wi-Fi was up. The HTML login page is optional. Its longest header line was
  1841 bytes; the read buffer is 4096 bytes.
- When login fails, the screen names the class of failure (memory, network or
  TLS, HTTP status, compressed body, parse, QR image, or a short server
  message) and the current step (`step: enter`, `step: dns`, `step: tls start`).
  Those steps stay in RTC memory. The card is written only at the next boot,
  appending `esp_reset_reason()` and the steps to `/.crosspoint/fanqie/fanqie.log`
  with `O_AT_END` (the file is not truncated). At 64KB it is renamed to
  `fanqie.log.1`. Cookie and token text is not stored.
- Shelf download and progress upload were not executed against a signed-in
  account from this workspace. Signature acceptance and the progress unit can
  change without notice. A failed download leaves the placeholder page and
  does not invent chapter text.
- At most 32 shelf books and 600 chapters are indexed per book.
- After Wi-Fi returns, login paints once (`step: tls`) and returns to
  `loop()`, which is what WeRead's `startJob` does. The next turn appends
  `tls` to `fanqie.log` without painting again, turns Wi-Fi modem sleep off,
  `12a8ae5` still panicked in `dns_send` after `heap_caps_check_integrity_all`
  returned ok, with 94KB internal DMA free. `RenderLock` is a FreeRTOS mutex.
  WeRead's login GET holds it. `NetworkStartup::prepare` only reclaims fonts
  when memory is already low; it does not change Wi-Fi mode or power-save.
  This image does not hold the mutex for the QR step. It logs mode, power-save,
  and channel, calls `prepare`, resolves `weread.qq.com`, resolves
  `fanqienovel.com`, then performs the login GET. The first flushed step
  without its `ok` line is the panic. Stage lines `tls-conn`, `tls-req`,
  `tls-hdr`, and `tls-body` remain. RTC memory does not survive a power cut.
  Fanqie does not drive the power latch; `preventAutoSleep` stays true.
- Login, shelf sync, opening a book, and exit upload use
  `WifiSelectionActivity` when `WiFi.status()` is not already `WL_CONNECTED`.
  That screen auto-connects a saved network, then shows the system network
  list. Cancel returns to the Fanqie offline message. `FanqieNet::releaseRadio`
  turns the station off when the Fanqie activity exits. Reading does not keep
  the radio up.
- A card may contain `/Fanqie/<bookId>.epub` without
  `/.crosspoint/fanqie/index.bin`. The browser emulator refuses to import a
  root `.crosspoint` directory, and a real card can lose that index too.
  `findByEpubPath` rebuilds it from digit-named EPUB files directly under
  `/Fanqie/`. Creating that cache directory treats an existing directory as
  success: SdFat's `mkdir` returns false when `/Fanqie` is already on the
  card, and treating that as failure skipped the rebuild. The stored title is
  the book id until a shelf sync replaces it. Opening still uses the existing
  reader. With no chapter map, leaving the reader keeps the spine and page and
  does not replace the percent the reader already wrote.
- The X4 Pro has no confirm key. The disclaimer and the result message accept
  a screen tap. The account list already activates the tapped row.
- The capacitive Home key calls `ReaderActivity::handleHomeGesture()`, which
  uses the same `onGoHome()` path as Back. A Fanqie book therefore runs the
  upload activity on the way home. The reader still writes its own shelf
  percent in `onExit()` before that upload reads `progress.bin`.
- Account records are not kept on the task stack. Each cookie is 1536 bytes,
  and four of them plus a font draw overflow the 16KB render-task stack. That
  overflow rewrote `renderingMutex` so it was no longer a semaphore; the next
  `xSemaphoreTake` asserted `uxItemSize == 0` in `queue.c` and rebooted the
  device. `RenderLock::peek` also no longer calls `xQueuePeek` on that mutex.
- A successful `pio run -e x4pro`, a first paint, or a button ACK is not
  functional acceptance. The browser emulator does not simulate Wi-Fi, so it
  cannot prove login or sync. It also cannot replace a device check of radio
  timing or power.

## Emulator, round 2

Device `xteink_x4pro`, automation session, factory image SHA-256
`503a9f648028fbbbaf6722dac4553b7f0938305365f4d66098dfdf9e279b57cf`
(app partition 6386354 / 6553600, 97.4%). The card import was only
`Fanqie/7000000000000000001.epub`. Screenshots are `#browser-screen`.

Passed:

- Onboarding completed by a tap on 简体中文. The home labels were Chinese.
- The file browser opened that EPUB. The log then entered `FanqieSync` and
  the panel showed 「正在打开已缓存的书」. The existing reader showed
  “First fixture page of the cached Fanqie book…”.
- Page-down replaced that sentence with “Second fixture page…”. The reader
  footer moved from 5.3% to 100%.
- Home’s first shelf row was “Fanqie Fixture” / CrossMux with chapter
  progress 100%.
- After `stop()` and `start()`, the same shelf frame came back (screenshot
  SHA-256 `7afbccad42b6af1b22c93e02a3626b28f1bcf26c239c0c24ee18536db253e542`).

Not passed:

- The Fanqie account page. One run drew the disclaimer
  （「这是番茄小说的非官方客户端…」） and did not crash. After that acceptance
  was stored, opening the account list again stopped producing serial output
  after the missing `accounts.bin` open. No `displayBuffer` followed, and a
  further touch was rejected by the emulator kernel. The offline sentence
  「没有网络。已缓存的书仍可从主页打开。」 was not captured. This does not
  prove a crash, and it does not prove the offline screen.

Logs, including batches marked truncated, are under
`fanqie-x4pro/round2/` in the run artifacts.

## On-card layout

See [file-formats.md](../file-formats.md#fanqie-cache). Bump the matching
version constant before changing a binary record.
