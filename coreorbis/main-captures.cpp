// Captures (2026-10-10, AI-assisted): the app's entry point (eboot.bin) -- a QR code on the TV for the phone.
//
// The app asks to leave its sandbox (the helper, as WHEN98 does), keeps a connection to the helper's Captures service
// (ProsperoCaptures.h: the gallery on the LAN runs while it is open) and shows its address as a QR code, with how many
// screenshots and videos there are and how many files went to the phone. Circle or OPTIONS closes it, and the
// gallery with it.
//
// SPDX-License-Identifier: MIT

#include "OrbisPaths.h"
#include "ProsperoCaptures.h"
#include "ProsperoCrash.h"
#include "ProsperoInput.h"
#include "ProsperoJailbreak.h"
#include "ProsperoNotify.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include "fe_text.h"
#include "third_party/qrcodegen/qrcodegen.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#ifndef GENPLUS_PS5_VERSION
#define GENPLUS_PS5_VERSION "dev"
#endif

namespace
{
using ps5video::kHeight;
using ps5video::kWidth;
using ps5video::Rgb;

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

struct Status
{
	std::string ip;
	int port = 0, photos = 0, videos = 0, web = 0;
	int conv_left = 0, conv_pct = 0; // clips still to make into MP4s, the current one's progress
	unsigned sent = 0;
	bool connected = false;
};

// The helper's service: one line a second ("IP .. PORT .. PHOTOS .. VIDEOS .. SENT .. WEB ..").
class Service
{
public:
	~Service()
	{
		if (m_fd >= 0)
			close(m_fd);
	}
	void Poll(Status& st)
	{
		if (m_fd < 0 && Now() >= m_retry)
		{
			m_retry = Now() + 2.0;
			m_fd = socket(AF_INET, SOCK_STREAM, 0);
			sockaddr_in a = {};
			a.sin_family = AF_INET;
			a.sin_port = htons(captures::kControlPort);
			a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			if (m_fd < 0 || connect(m_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0)
			{
				if (m_fd >= 0)
					close(m_fd);
				m_fd = -1;
				st.connected = false;
				return;
			}
			fcntl(m_fd, F_SETFL, fcntl(m_fd, F_GETFL) | O_NONBLOCK);
			OrbisLog("[captures] connected to the helper's service");
		}
		if (m_fd < 0)
			return;
		char buf[512];
		const ssize_t n = recv(m_fd, buf, sizeof(buf), 0);
		if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
		{
			close(m_fd);
			m_fd = -1;
			st.connected = false;
			return;
		}
		if (n > 0)
			m_line.append(buf, size_t(n));
		size_t nl;
		while ((nl = m_line.find('\n')) != std::string::npos)
		{
			const std::string l = m_line.substr(0, nl);
			m_line.erase(0, nl + 1);
			char ip[64] = "";
			if (sscanf(l.c_str(), "IP %63s PORT %d PHOTOS %d VIDEOS %d SENT %u WEB %d", ip, &st.port, &st.photos, &st.videos,
					&st.sent, &st.web) == 6)
			{
				st.ip = strcmp(ip, "-") == 0 ? "" : ip;
				st.connected = true;
				const char* conv = strstr(l.c_str(), " CONV "); // (Captures 1.2's helper)
				if (!conv || sscanf(conv, " CONV %d %d", &st.conv_left, &st.conv_pct) != 2)
					st.conv_left = st.conv_pct = 0;
			}
		}
	}

private:
	int m_fd = -1;
	double m_retry = 0;
	std::string m_line;
};

void Center(int y, const std::string& s, int scale, uint32_t color)
{
	const std::string fit = fe::FitText(s, scale, kWidth - 160);
	fe::DrawText((kWidth - fe::TextWidth(fit.c_str(), scale)) / 2, y, fit.c_str(), scale, color);
}

// A QR code of `text` in a white card with its quiet zone, `size` pixels square at (x, y).
void DrawQr(const std::string& text, int x, int y, int size)
{
	uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
	ps5video::FillRect(x, y, size, size, Rgb(255, 255, 255));
	if (!qrcodegen_encodeText(text.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
			qrcodegen_Mask_AUTO, true))
		return;
	const int n = qrcodegen_getSize(qr), quiet = 4;
	const int cell = size / (n + 2 * quiet), ox = x + (size - cell * n) / 2, oy = y + (size - cell * n) / 2;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
			if (qrcodegen_getModule(qr, i, j))
				ps5video::FillRect(ox + i * cell, oy + j * cell, cell, cell, Rgb(16, 18, 26));
}

void Draw(const Status& st, double t)
{
	// background: dark, a soft glow behind the code
	for (int y = 0; y < kHeight; y += 4)
	{
		const float k = float(y) / kHeight;
		ps5video::FillRect(0, y, kWidth, 4, Rgb(uint8_t(14 + 6 * (1 - k)), uint8_t(16 + 8 * (1 - k)), uint8_t(24 + 18 * (1 - k))));
	}
	fe::DrawText(80, 46, "Captures", 6, Rgb(120, 160, 255));
	fe::DrawText(84, 118, "Screenshots and videos to your phone - v" GENPLUS_PS5_VERSION, 2, Rgb(150, 155, 180));

	const int qs = 470, qx = 250, qy = (kHeight - qs) / 2 + 10;
	const int tx = qx + qs + 120;
	if (!st.connected)
	{
		const int dots = int(t * 2) % 4;
		Center(kHeight / 2 - 30, std::string("Starting") + std::string(size_t(dots), '.'), 5, Rgb(230, 232, 240));
	}
	else if (st.ip.empty() || !st.web)
	{
		Center(kHeight / 2 - 60, st.ip.empty() ? "The PS5 isn't on a network" : "The gallery couldn't start", 5, Rgb(240, 200, 120));
		Center(kHeight / 2 + 20, st.ip.empty() ? "Connect it to your Wi-Fi or a cable, then open Captures again."
											 : "Close Captures and open it again.", 3, Rgb(190, 192, 210));
	}
	else
	{
		const std::string url = "http://" + st.ip + ":" + std::to_string(st.port);
		ps5video::FillRect(qx - 14, qy - 14, qs + 28, qs + 28, Rgb(91, 140, 255)); // a frame in the accent colour
		DrawQr(url, qx, qy, qs);
		fe::DrawText(tx, qy + 10, "Scan with your phone", 5, Rgb(240, 242, 250));
		fe::DrawText(tx, qy + 80, "Phone and PS5 on the same Wi-Fi, open the camera and point it at the code.", 3,
			Rgb(175, 180, 200));
		fe::DrawText(tx, qy + 150, "Or type this in the phone's browser:", 3, Rgb(175, 180, 200));
		fe::DrawText(tx, qy + 195, url.c_str(), 5, Rgb(255, 196, 80));
		char counts[128];
		snprintf(counts, sizeof(counts), "%d screenshot%s   %d video%s", st.photos, st.photos == 1 ? "" : "s", st.videos,
			st.videos == 1 ? "" : "s");
		fe::DrawText(tx, qy + 300, counts, 4, Rgb(230, 232, 240));
		// (only while the phone is saving videos: the WebM ones made into MP4s)
		if (st.conv_left > 0)
		{
			char conv[128];
			snprintf(conv, sizeof(conv), "Making %d video%s into MP4 for the phone... %d%%", st.conv_left,
				st.conv_left == 1 ? "" : "s", st.conv_pct);
			fe::DrawText(tx, qy + 360, conv, 3, Rgb(255, 196, 80));
		}
		fe::DrawText(tx, qy + 430, "The gallery only runs while this screen is open.", 2, Rgb(130, 135, 160));
	}
	const std::string hint = std::string(fe::icon::Circle) + "  Close";
	fe::DrawText((kWidth - fe::TextWidth(hint.c_str(), 3)) / 2, kHeight - 80, hint.c_str(), 3, Rgb(170, 175, 200));
	ps5video::Present(0, 0, 0, 0, true);
}

[[noreturn]] void ExitApp(int status)
{
	OrbisLog("[boot] exit %d", status);
	ProsperoNotifyFlush();
	OrbisLogClose();
	fflush(stdout);
	const int rc = sceSystemServiceLoadExec("exit", nullptr);
	if (rc == 0)
		for (int i = 0; i < 100; i++)
			usleep(100 * 1000);
	_exit(status);
}

int Run()
{
	setvbuf(stdout, nullptr, _IOLBF, 0);
	crashlog::Install();
	OrbisLog("[boot] Captures %s (built %s %s), pid %d", GENPLUS_PS5_VERSION, __DATE__, __TIME__, int(getpid()));
	std::string how;
	const bool freed = jailbreak::RequestForSelf(how);
	OrbisLog("[boot] jailbreak: %s", freed ? how.c_str() : "nobody answered");
	OrbisPathsInit();
	OrbisLogOpen();
	if (!ps5video::Init())
	{
		ProsperoNotify("Captures: video failed to start");
		return 1;
	}
	sceSystemServiceHideSplashScreen();
	ps5input::Init();
	Service service;
	Status st;
	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).buttons;
	double last_draw = 0;
	Status shown;
	for (;;)
	{
		service.Poll(st);
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons, down = cur & ~prev;
		prev = cur;
		if (down & (SCE_PAD_BUTTON_CIRCLE | SCE_PAD_BUTTON_OPTIONS))
			break;
		const double now = Now();
		const bool changed = st.connected != shown.connected || st.ip != shown.ip || st.photos != shown.photos ||
			st.videos != shown.videos || st.sent != shown.sent || st.web != shown.web || st.conv_left != shown.conv_left ||
			st.conv_pct != shown.conv_pct;
		if (changed || now - last_draw > (st.connected ? 5.0 : 0.5))
		{
			Draw(st, now);
			shown = st;
			last_draw = now;
		}
		usleep(16000);
	}
	OrbisLog("[boot] leaving");
	ps5input::Shutdown();
	ps5video::Shutdown();
	return 0;
}

void* RunThread(void* p)
{
	*static_cast<int*>(p) = Run();
	return nullptr;
}
} // namespace

int main()
{
	int status = 1;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
	pthread_t t;
	if (pthread_create(&t, &attr, RunThread, &status) == 0)
		pthread_join(t, nullptr);
	else
		status = Run();
	ExitApp(status);
}
