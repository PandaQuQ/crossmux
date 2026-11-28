#pragma once

namespace FanqieLog {

// Record a step in RAM and in RTC memory. No SD access: the card and the panel
// must not be touched from a Wi-Fi callback or while a refresh is in progress.
void line(const char* text);

// Same record, then one appended line on the card. Call it only after the panel
// refresh has returned, and never while holding RenderLock.
void persist(const char* text);

// After the card is mounted and before any refresh: append the reset reason
// and every step that survived esp_restart. Power loss clears the RTC copy.
void noteBoot();

}  // namespace FanqieLog

// Defined for every build. Marks the reason immediately before esp_restart.
extern "C" void fanqieNoteRestart(const char* why);
