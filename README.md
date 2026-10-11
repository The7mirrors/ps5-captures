<p align="center"><img src="app/sce_sys/icon0.png" width="160" alt="Captures icon"></p>

<h1 align="center">Captures for PS5</h1>

<p align="center"><b>Send your PS5 screenshots and videos to your phone over Wi-Fi.</b><br>
Open the app, scan the QR code, browse and save. No account, no cloud, no cable.</p>

---

## What it does

The PS5 keeps every screenshot and video clip you capture, but getting them onto your phone normally needs the
PlayStation App and a PSN connection. **Captures** is a small homebrew app that serves your captures straight from the
console to any phone or computer on the same network:

- **Scan and go** - the TV shows a big QR code; point your phone's camera at it and the gallery opens in the browser.
- **A real gallery** - every screenshot and video, newest first, grouped by day, with the console's own thumbnails.
- **Pick a game** - a picker with each game's icon, name and number of captures (PS5 and PS4 titles).
- **View and play** - full-screen pictures, videos play right in the browser (with seeking), swipe between them.
- **Save one or many** - save a single file, or select several and save them one by one (or as one `.zip`).
- **Phone-friendly videos** - clips that some homebrew apps record as WebM are made into MP4 on the PS5 when you
  save them, so phone galleries take them.
- **Nothing to set up** - one button on the PS5 (Circle to close).

## Safe by design

- The gallery only runs **while the Captures screen is open**; close the app and it stops.
- It only **reads** the capture folders (`/user/av_contents/photo` and `/video`) - nothing on the console is changed
  or deleted.
- It is only reachable from your **local network** (the PS5's own address, port 8090). Nothing goes to the internet.

## Requirements

- A **jailbroken PS5** with a payload loader (for example the PS5 Payload Manager / `elfldr` on port 9021).
  Tested on firmware **13.00**.
- **ShadowMountPlus** (or another homebrew registrar) so the installed app gets its home-screen tile.
- A phone or computer on the **same network** as the PS5.

## Install

1. Download **`CapturesPS5-v1.2.zip`** from the [Releases](../../releases) page and unzip it to get
   **`CapturesPS5.elf`**.
2. Send it with your payload loader (Payload Manager, `elfldr`, ...).
   It installs the app to `/data/homebrew/PPSA99233` and stays running as the app's helper.
3. Open **Captures** from the home screen (if the tile doesn't appear, let ShadowMountPlus rescan or restart the PS5).

After a reboot you don't need to send the payload again: when no helper answers, the app sends its own helper to the
ELF loader (`elfldr`, port 9021) by itself.

## Use

1. Open **Captures** on the PS5. The TV shows a QR code and the address, e.g. `http://192.168.1.20:8090`.
2. Scan the code with your phone (or type the address in the phone's browser).
3. Browse, tap to view, **Save** to download. **Select** lets you pick several and save them (each as its own file,
   or all in one `.zip` for a computer).
4. Press **Circle** on the controller to close Captures (the gallery stops with it).

**iPhone tip:** press and hold a picture and choose *Save to Photos*. Downloaded videos land in the Files app, from
where *Share -> Save Video* puts them in Photos.

**WebM videos:** clips recorded while some homebrew apps run are WebM files, which phone galleries refuse. When you
save one, the PS5 first makes an MP4 of it (the page shows the progress; usually under a minute) and keeps it in
`/data/captures/mp4/`, so saving it again is instant. Only the clips you save are converted.

## What's new

- **1.2** - WebM clips are made into MP4 on the PS5 when you save them (FFmpeg + x264 in the helper), with the
  progress on the phone and the TV; the `.zip` carries the MP4s; several picked files save one by one, so phones keep
  them as normal files; the "files sent" counter is gone.
- **1.1** - downloads are sent as files, so phones save videos instead of opening them in a player; WebM clips are
  served as their MP4 copy when one is placed in `/data/captures/mp4/`.
- **1.0** - first release.

## Build from source

Needs Linux (or WSL), the [ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk), clang/lld 18, zlib, nasm and
curl. First build FFmpeg and x264 for the PS5 (downloads FFmpeg 7.1.5 and a pinned x264 into `deps/`), then the app:

```sh
PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk deps/build-deps.sh
make PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
# -> build/ps5/CapturesPS5.elf
```

How it is built:

| Part | Files | What it is |
| --- | --- | --- |
| The app | `coreorbis/main-captures.cpp` | the QR screen (a native PS5 title, `eboot.bin`) |
| The gallery | `coreorbis/orbis-shims/ProsperoCaptures.cpp`, `captures_page.inc` | the web server and the phone page, run by the helper |
| The converter | `coreorbis/orbis-shims/ProsperoConvert.cpp`, `deps/build-deps.sh` | WebM -> MP4 (H.264 + AAC) on the PS5 with FFmpeg and x264 |
| The helper | `coreorbis/orbis-shims/ProsperoHelper.cpp` | a payload that takes the app out of its sandbox and serves the gallery |
| The installer | `installer/installer_main.cpp`, `ProsperoInstall.cpp` | installs the app folder built into the payload |
| PS5 layer | `coreorbis/orbis-shims/*` | video, pad, notifications, paths, crash log |

## Credits

- **[genplusgxPS5](https://github.com/MisterTemaki/genplusgxPS5)** by MisterTemaki - the PS5 app shell this grew
  from (video, pad, text, the installer/helper layout and the sandbox escape).
- **[PS5SX2](https://github.com/Swordpdf/PS5SX2)** by swordpdf - the native-title, installer and helper approach
  that shell follows.
- **[ps5-native-app-boilerplate](https://github.com/blackbearreloaded)** by BlackBearReloaded - the native app build
  tools and startup code in `proto/native` (GPL-3.0).
- **[ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk)** - the toolchain and PS5 libraries.
- **[FFmpeg](https://ffmpeg.org)** (LGPL-2.1+, built with `--enable-gpl`) and **[x264](https://www.videolan.org/developers/x264.html)**
  (GPL-2.0-or-later) - the WebM -> MP4 converter in the helper; `deps/build-deps.sh` fetches and builds the exact
  sources used.
- **[QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library)** by Project Nayuki (MIT).
- **[stb_truetype](https://github.com/nothings/stb)** by Sean Barrett (public domain / MIT).
- Fonts: **Roboto** (Apache 2.0), **PromptFont** by Yukari "Shinmera" Hafner (SIL OFL), **Font Awesome** brands
  (SIL OFL) - their licenses are in `frontend/assets/fonts`.

Made with the help of an AI assistant (Claude by Anthropic).

## License

The project as a whole is released under the **GNU General Public License v3.0 or later** (see [LICENSE](LICENSE)),
because the app links the GPL-3.0 startup code of ps5-native-app-boilerplate and the helper links FFmpeg (built
with `--enable-gpl`) and x264 (GPL-2.0-or-later). The files written for this project and
the PS5 layer carry their own MIT notices and may also be used under MIT on their own.

*Captures is an unofficial homebrew project, not affiliated with or endorsed by Sony Interactive Entertainment.
"PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc. Use at your own risk.*
