// Captures (2026-10-11, AI-assisted): a WebM clip (the PS5 records homebrew apps' clips as WebM: VP9 + Opus, which
// phones' galleries refuse) made into an MP4 (H.264 + AAC) that they take, on the console itself - FFmpeg's demuxer,
// decoders and AAC encoder, x264 for the H.264.
// SPDX-License-Identifier: GPL-3.0-or-later (it links x264 and FFmpeg built with --enable-gpl)
#pragma once

#include <atomic>
#include <string>

namespace captures
{
// Writes `out` (an MP4) from `in` (a WebM / Matroska clip). `percent` follows the progress (0..100); a true `stop`
// ends it early (false is returned, `out` may be left half written - the caller removes it). `error` says why it
// failed. Uses a few threads of its own.
bool ConvertToMp4(const std::string& in, const std::string& out, std::atomic<int>* percent, const std::atomic<bool>* stop,
	std::string* error);
} // namespace captures
