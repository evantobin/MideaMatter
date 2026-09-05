#pragma once

namespace web_log {

// Starts the rolling log capture and exposes it over HTTP after Wi-Fi receives
// an IPv4 address.
void begin();

}  // namespace web_log
