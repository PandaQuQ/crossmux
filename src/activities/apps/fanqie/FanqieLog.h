#pragma once

namespace FanqieLog {

// Append one line to /.crosspoint/fanqie/fanqie.log and flush it before
// returning. The line must not contain a cookie, token, or QR payload.
void line(const char* text);

// First line of a boot: reset reason, then the last line already on the card.
void noteBoot();

}  // namespace FanqieLog
