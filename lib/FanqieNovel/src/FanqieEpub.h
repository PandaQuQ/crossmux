#pragma once

namespace FanqieEpub {

// Builds a stored-method EPUB the existing reader can open. Cached chapter
// text is included; missing chapters become a short placeholder page so the
// spine index stays stable.
bool build(const char* bookId);

}  // namespace FanqieEpub
