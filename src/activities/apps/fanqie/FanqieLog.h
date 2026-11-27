#pragma once

namespace FanqieLog {

// Append one line to /.crosspoint/fanqie/fanqie.log. The line must not contain
// a cookie, token, or QR payload. Values that look like those are blanked.
void line(const char* text);

}  // namespace FanqieLog
