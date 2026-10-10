// Captures (2026-10-10, AI-assisted): the PS5's screenshots and videos, to a phone on the same Wi-Fi.
//
// The helper payload serves a gallery page on the LAN (port kWebPort) while the Captures app is open: the app keeps a
// connection to 127.0.0.1:kControlPort, and the helper sends it a status line every second ("IP <ip> PORT <port>
// PHOTOS <n> VIDEOS <n> SENT <files>"). When the app goes, the web server stops. It only reads
// /user/av_contents/{photo,video}/... and the console's own thumbnails of them -- nothing is written or removed.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace captures
{
constexpr uint16_t kWebPort = 8090;
constexpr uint16_t kControlPort = 9093;

// The helper side: a thread serving kControlPort (and kWebPort while the app is connected). Returns at once.
void StartServer();
} // namespace captures
