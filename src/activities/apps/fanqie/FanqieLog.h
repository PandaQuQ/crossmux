#pragma once

namespace FanqieLog {

// Record a step in RAM and in RTC memory. No SD access: the card and the panel
// must not be touched from a Wi-Fi callback or while a refresh is in progress.
void line(const char* text);

// Optional. Called after the step is stored. The activity uses it to paint the
// step. It must not call line() again.
void setShow(void (*fn)(const char* text));

// After the card is mounted and before any refresh: append the reset reason
// and every step that survived esp_restart. Power loss clears the RTC copy.
void noteBoot();

}  // namespace FanqieLog

// Defined for every build. Marks the reason immediately before esp_restart.
extern "C" void fanqieNoteRestart(const char* why);
