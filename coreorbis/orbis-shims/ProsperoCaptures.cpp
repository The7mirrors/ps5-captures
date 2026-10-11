// Captures (2026-10-10, AI-assisted): the helper's gallery server (ProsperoCaptures.h).
//
//   GET  /             the gallery page (captures_page.inc)
//   GET  /api/list     {"games":{"PPSA..":"name"},"items":[{"id":"photo/PPSA../0f4/x.jpg","t":"p","g":"PPSA..","s":size,"m":mtime}]}
//   GET  /t/<id>       its thumbnail (the console's own, under /user/av_contents/thumbnails), else the photo itself
//   GET  /f/<id>       the file, with Range (phones play videos by ranges); ?dl=1 makes it a download
//   POST /zip          ids=<id>%0A<id>... -> one .zip of them, stored (no compression; files under 4 GB in all)
//
// An id is always "photo|video/<title id>/<folder>/<file>": letters, digits, '_', '-', '.', no "..": nothing outside
// the capture folders can be asked for.
//
// SPDX-License-Identifier: MIT

#include "ProsperoCaptures.h"

#include "OrbisPaths.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace captures
{
namespace
{
const char kRoot[] = "/user/av_contents";
const char kPage[] =
#include "captures_page.inc"
	;

std::atomic<int> g_listen{-1};      // the web server's socket while the app is connected
std::atomic<unsigned> g_sent{0};    // files sent since the app opened
std::atomic<int> g_photos{0}, g_videos{0};

struct Item
{
	std::string id, game;
	bool video = false;
	long long size = 0;
	long long mtime = 0;
};

bool SafeId(const std::string& id)
{
	if (id.size() < 10 || id.size() > 300 || id.find("..") != std::string::npos)
		return false;
	if (id.rfind("photo/", 0) != 0 && id.rfind("video/", 0) != 0)
		return false;
	int slashes = 0;
	for (char c : id)
	{
		if (c == '/')
			slashes++;
		else if (!(isalnum(uint8_t(c)) || c == '_' || c == '-' || c == '.'))
			return false;
	}
	return slashes == 3;
}

// "photo/PPSA99232/0f4/x.jpg" -> /user/av_contents/photo/NPXS40087/PPSA99232/0f4/x.jpg
std::string FilePath(const std::string& id)
{
	const size_t s = id.find('/');
	return std::string(kRoot) + "/" + id.substr(0, s) + "/NPXS40087" + id.substr(s);
}

// A WebM clip made into an MP4 (on a PC: phones' galleries take MP4, not WebM): /data/captures/mp4/<game>/<nnn>/<name>.mp4,
// or "" if there is none. "video/PPSA99232/0f4/x.webm" -> /data/captures/mp4/PPSA99232/0f4/x.mp4
std::string Mp4Path(const std::string& id)
{
	if (id.size() < 12 || id.compare(0, 6, "video/") != 0 || id.compare(id.size() - 5, 5, ".webm") != 0)
		return "";
	const std::string p = "/data/captures/mp4/" + id.substr(6, id.size() - 11) + ".mp4";
	struct stat st;
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? p : "";
}

std::string ThumbPath(const std::string& id)
{
	const size_t s = id.find('/');
	return std::string(kRoot) + "/thumbnails/" + id.substr(0, s) + "/NPXS40087" + id.substr(s) + ".jpeg";
}

bool Ext(const std::string& n, std::initializer_list<const char*> exts)
{
	std::string lower = n;
	for (char& c : lower)
		c = char(tolower(uint8_t(c)));
	for (const char* e : exts)
	{
		const size_t l = strlen(e);
		if (lower.size() > l && lower.compare(lower.size() - l, l, e) == 0)
			return true;
	}
	return false;
}

std::vector<Item> Scan()
{
	std::vector<Item> out;
	for (const char* kind : {"photo", "video"})
	{
		const std::string base = std::string(kRoot) + "/" + kind + "/NPXS40087";
		DIR* games = opendir(base.c_str());
		if (!games)
			continue;
		while (struct dirent* g = readdir(games))
		{
			if (g->d_name[0] == '.')
				continue;
			const std::string gdir = base + "/" + g->d_name;
			DIR* subs = opendir(gdir.c_str());
			if (!subs)
				continue;
			while (struct dirent* s = readdir(subs))
			{
				if (s->d_name[0] == '.')
					continue;
				const std::string sdir = gdir + "/" + s->d_name;
				DIR* files = opendir(sdir.c_str());
				if (!files)
					continue;
				while (struct dirent* f = readdir(files))
				{
					const std::string n = f->d_name;
					const bool video = kind[0] == 'v';
					if (!(video ? Ext(n, {".mp4", ".webm"}) : Ext(n, {".jpg", ".jpeg", ".png", ".webp"})))
						continue;
					struct stat st;
					if (stat((sdir + "/" + n).c_str(), &st) != 0)
						continue;
					Item it;
					it.id = std::string(kind) + "/" + g->d_name + "/" + s->d_name + "/" + n;
					it.game = g->d_name;
					it.video = video;
					it.size = st.st_size;
					it.mtime = st.st_mtime;
					if (SafeId(it.id))
						out.push_back(it);
				}
				closedir(files);
			}
			closedir(subs);
		}
		closedir(games);
	}
	std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.mtime > b.mtime; });
	int p = 0, v = 0;
	for (const Item& it : out)
		(it.video ? v : p)++;
	g_photos = p;
	g_videos = v;
	return out;
}

std::string JsonEscape(const std::string& s)
{
	std::string o;
	for (char c : s)
	{
		if (c == '"' || c == '\\')
			o += '\\';
		if (uint8_t(c) >= 0x20)
			o += c;
	}
	return o;
}

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// A PS4 game's TITLE: param.sfo (entry 0x1000) out of its installed app.pkg, read in place (the table is plain).
std::string Ps4Title(const std::string& tid)
{
	for (const char* base : {"/user/app/", "/mnt/ext0/user/app/", "/mnt/ext1/user/app/"})
	{
		const int fd = open((std::string(base) + tid + "/app.pkg").c_str(), O_RDONLY);
		if (fd < 0)
			continue;
		uint8_t h[32];
		std::string title;
		if (pread(fd, h, sizeof(h), 0) == ssize_t(sizeof(h)) && Be32(h) == 0x7F434E54)
		{
			const uint32_t count = Be32(h + 0x10), table = Be32(h + 0x18);
			std::vector<uint8_t> t(size_t(std::min<uint32_t>(count, 4096)) * 32);
			if (pread(fd, t.data(), t.size(), table) == ssize_t(t.size()))
				for (size_t i = 0; i + 32 <= t.size(); i += 32)
					if (Be32(&t[i]) == 0x1000) // param.sfo
					{
						const uint32_t off = Be32(&t[i + 16]), size = Be32(&t[i + 20]);
						std::vector<uint8_t> sfo(std::min<uint32_t>(size, 65536));
						if (pread(fd, sfo.data(), sfo.size(), off) != ssize_t(sfo.size()) || sfo.size() < 20 || Le32(&sfo[0]) != 0x46535000)
							break;
						const uint32_t keys = Le32(&sfo[8]), data = Le32(&sfo[12]), n = Le32(&sfo[16]);
						for (uint32_t e = 0; e < n && 20 + e * 16 + 16 <= sfo.size(); e++)
						{
							const uint8_t* ix = &sfo[20 + e * 16];
							const uint32_t ko = keys + (ix[0] | ix[1] << 8), len = Le32(ix + 4), dofs = data + Le32(ix + 12);
							if (ko < sfo.size() && strcmp(reinterpret_cast<const char*>(&sfo[ko]), "TITLE") == 0 && dofs + len <= sfo.size())
							{
								title.assign(reinterpret_cast<const char*>(&sfo[dofs]), len);
								title = title.c_str(); // up to its NUL
							}
						}
						break;
					}
		}
		close(fd);
		if (!title.empty())
			return title;
	}
	// else the English words the console uses for voice commands
	if (FILE* f = fopen(("/user/appmeta/" + tid + "/pronunciation.xml").c_str(), "rb"))
	{
		std::string x;
		char buf[4096];
		size_t r;
		while ((r = fread(buf, 1, sizeof(buf), f)) > 0 && x.size() < 65536)
			x.append(buf, r);
		fclose(f);
		size_t lang = x.find("<language id=\"01\"");
		if (lang == std::string::npos)
			lang = 0;
		const size_t a = x.find('>', x.find("<text", lang));
		const size_t b = a == std::string::npos ? a : x.find("</text>", a);
		if (b != std::string::npos)
			return x.substr(a + 1, b - a - 1);
	}
	return "";
}

// A title's name: its home-screen tile's param.json (titleName), a PS4 game's param.sfo, cached; the system's as "PS5".
std::string GameName(const std::string& tid)
{
	static std::mutex lock;
	static std::map<std::string, std::string> cache;
	std::lock_guard<std::mutex> g(lock);
	auto it = cache.find(tid);
	if (it != cache.end())
		return it->second;
	std::string name = tid.rfind("NPXS", 0) == 0 ? "PS5" : tid;
	for (const std::string& p : {"/user/appmeta/" + tid + "/param.json", "/system_ex/app/" + tid + "/sce_sys/param.json"})
		if (FILE* f = fopen(p.c_str(), "rb"))
		{
			std::string j;
			char buf[8192];
			size_t n;
			while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && j.size() < 65536)
				j.append(buf, n);
			fclose(f);
			const size_t k = j.find("\"titleName\"");
			if (k != std::string::npos)
			{
				const size_t q1 = j.find('"', j.find(':', k) + 1);
				const size_t q2 = q1 == std::string::npos ? q1 : j.find('"', q1 + 1);
				if (q2 != std::string::npos && q2 > q1 + 1)
				{
					name = j.substr(q1 + 1, q2 - q1 - 1);
					break;
				}
			}
		}
	if (name == tid && tid.rfind("CUSA", 0) == 0)
	{
		const std::string t = Ps4Title(tid);
		if (!t.empty())
			name = t;
	}
	cache[tid] = name;
	return name;
}

// ---- HTTP ----------------------------------------------------------------------------------------------------
bool SendAll(int s, const void* p, size_t n)
{
	const char* b = static_cast<const char*>(p);
	while (n)
	{
		const ssize_t w = send(s, b, n, 0);
		if (w <= 0)
			return false;
		b += w;
		n -= size_t(w);
	}
	return true;
}

bool SendText(int s, int code, const char* type, const std::string& body, const std::string& extra = "")
{
	char head[512];
	const int n = snprintf(head, sizeof(head),
		"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n%s\r\n", code,
		code == 200 ? "OK" : code == 404 ? "Not Found" : code == 413 ? "Too Large" : "Bad Request", type, body.size(),
		extra.c_str());
	return SendAll(s, head, size_t(n)) && SendAll(s, body.data(), body.size());
}

std::string UrlDecode(const std::string& s)
{
	std::string o;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] == '%' && i + 2 < s.size())
		{
			o += char(strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
			i += 2;
		}
		else
			o += s[i] == '+' ? ' ' : s[i];
	}
	return o;
}

const char* Mime(const std::string& n)
{
	if (Ext(n, {".mp4"}))
		return "video/mp4";
	if (Ext(n, {".webm"}))
		return "video/webm";
	if (Ext(n, {".png"}))
		return "image/png";
	if (Ext(n, {".webp"}))
		return "image/webp";
	return "image/jpeg";
}

// "Game name 2026-10-10 12-14-10.jpg" for a download
std::string NiceName(const std::string& id)
{
	const size_t a = id.find('/'), b = id.find('/', a + 1), c = id.find_last_of('/');
	std::string name = GameName(id.substr(a + 1, b - a - 1)) + " " + id.substr(c + 1);
	for (char& ch : name)
		if (strchr("\\/:*?\"<>|", ch) || uint8_t(ch) < 0x20)
			ch = '_';
	return name;
}

void SendFile(int s, const std::string& path, const std::string& range, bool download, const std::string& nice)
{
	const int fd = open(path.c_str(), O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) != 0)
	{
		if (fd >= 0)
			close(fd);
		SendText(s, 404, "text/plain", "not found");
		return;
	}
	long long size = st.st_size, from = 0, to = size - 1;
	bool partial = false;
	if (range.rfind("bytes=", 0) == 0)
	{
		long long a = -1, b = -1;
		const std::string r = range.substr(6);
		const size_t dash = r.find('-');
		if (dash != std::string::npos)
		{
			if (dash > 0)
				a = atoll(r.substr(0, dash).c_str());
			if (dash + 1 < r.size())
				b = atoll(r.substr(dash + 1).c_str());
			if (a < 0 && b > 0) // the last b bytes
			{
				a = std::max(0LL, size - b);
				b = size - 1;
			}
			if (a >= 0 && a < size)
			{
				from = a;
				to = (b >= a && b < size) ? b : size - 1;
				partial = true;
			}
		}
	}
	char head[1024];
	int n = snprintf(head, sizeof(head), "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\nAccept-Ranges: bytes\r\n"
		"Cache-Control: max-age=3600\r\nConnection: close\r\n", partial ? "206 Partial Content" : "200 OK",
		download ? "application/octet-stream" : Mime(path), to - from + 1); // (a download: a file, not a video to play)
	if (partial)
		n += snprintf(head + n, sizeof(head) - size_t(n), "Content-Range: bytes %lld-%lld/%lld\r\n", from, to, size);
	if (download)
		n += snprintf(head + n, sizeof(head) - size_t(n), "Content-Disposition: attachment; filename=\"%s\"\r\n", nice.c_str());
	n += snprintf(head + n, sizeof(head) - size_t(n), "\r\n");
	if (SendAll(s, head, size_t(n)))
	{
		std::vector<char> buf(256 * 1024);
		long long left = to - from + 1, at = from;
		while (left > 0)
		{
			const ssize_t r = pread(fd, buf.data(), size_t(std::min<long long>(left, (long long)buf.size())), at);
			if (r <= 0 || !SendAll(s, buf.data(), size_t(r)))
				break;
			left -= r;
			at += r;
		}
		if (left == 0 && !partial)
			g_sent++;
	}
	close(fd);
}

// ---- ZIP (stored, data descriptors) ---------------------------------------------------------------------------
uint32_t Crc32(uint32_t crc, const uint8_t* p, size_t n)
{
	static uint32_t table[256];
	static bool made = false;
	if (!made)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		made = true;
	}
	crc = ~crc;
	while (n--)
		crc = table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	return ~crc;
}

void Put16(std::string& b, uint16_t v) { b += char(v & 0xff); b += char(v >> 8); }
void Put32(std::string& b, uint32_t v) { Put16(b, uint16_t(v)); Put16(b, uint16_t(v >> 16)); }

void SendZip(int s, const std::vector<std::string>& ids)
{
	long long total = 0;
	std::vector<std::pair<std::string, long long>> files;
	for (const std::string& id : ids)
	{
		struct stat st;
		if (!SafeId(id) || stat(FilePath(id).c_str(), &st) != 0)
			continue;
		files.push_back({id, st.st_size});
		total += st.st_size;
	}
	if (files.empty())
	{
		SendText(s, 404, "text/plain", "nothing to send");
		return;
	}
	if (total > 0xF0000000LL)
	{
		SendText(s, 413, "text/plain", "Too much at once (a zip holds 4 GB): pick fewer videos.");
		return;
	}
	char stamp[32];
	const time_t now = time(nullptr);
	strftime(stamp, sizeof(stamp), "%Y-%m-%d %H-%M", localtime(&now));
	char head[512];
	const int n = snprintf(head, sizeof(head),
		"HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Disposition: attachment; filename=\"PS5 captures %s.zip\"\r\n"
		"Cache-Control: no-store\r\nConnection: close\r\n\r\n", stamp);
	if (!SendAll(s, head, size_t(n)))
		return;
	std::string central;
	uint32_t offset = 0;
	std::vector<char> buf(256 * 1024);
	std::map<std::string, int> used;
	for (const auto& f : files)
	{
		std::string name = NiceName(f.first);
		if (used[name]++) // the same name twice: number it
			name.insert(name.find_last_of('.'), " (" + std::to_string(used[name]) + ")");
		std::string local;
		Put32(local, 0x04034b50);
		Put16(local, 20);
		Put16(local, 0x0808); // data descriptor follows, UTF-8 names
		Put16(local, 0);      // stored
		Put32(local, 0);      // time, date
		Put32(local, 0);      // crc (in the descriptor)
		Put32(local, 0);
		Put32(local, 0);
		Put16(local, uint16_t(name.size()));
		Put16(local, 0);
		local += name;
		if (!SendAll(s, local.data(), local.size()))
			return;
		const int fd = open(FilePath(f.first).c_str(), O_RDONLY);
		uint32_t crc = 0, size = 0;
		if (fd >= 0)
		{
			ssize_t r;
			while ((r = read(fd, buf.data(), buf.size())) > 0)
			{
				crc = Crc32(crc, reinterpret_cast<uint8_t*>(buf.data()), size_t(r));
				size += uint32_t(r);
				if (!SendAll(s, buf.data(), size_t(r)))
				{
					close(fd);
					return;
				}
			}
			close(fd);
		}
		std::string desc;
		Put32(desc, 0x08074b50);
		Put32(desc, crc);
		Put32(desc, size);
		Put32(desc, size);
		if (!SendAll(s, desc.data(), desc.size()))
			return;
		Put32(central, 0x02014b50);
		Put16(central, 20);
		Put16(central, 20);
		Put16(central, 0x0808);
		Put16(central, 0);
		Put32(central, 0);
		Put32(central, crc);
		Put32(central, size);
		Put32(central, size);
		Put16(central, uint16_t(name.size()));
		Put16(central, 0);
		Put16(central, 0);
		Put16(central, 0);
		Put16(central, 0);
		Put32(central, 0);
		Put32(central, offset);
		central += name;
		offset += uint32_t(local.size()) + size + uint32_t(desc.size());
		g_sent++;
	}
	std::string end;
	Put32(end, 0x06054b50);
	Put16(end, 0);
	Put16(end, 0);
	Put16(end, uint16_t(files.size()));
	Put16(end, uint16_t(files.size()));
	Put32(end, uint32_t(central.size()));
	Put32(end, offset);
	Put16(end, 0);
	SendAll(s, central.data(), central.size());
	SendAll(s, end.data(), end.size());
}

void* Client(void* arg)
{
	const int s = int(intptr_t(arg));
	timeval tv = {30, 0};
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
	std::string req;
	char buf[8192];
	size_t body_at = std::string::npos;
	while (req.size() < 256 * 1024)
	{
		const ssize_t r = recv(s, buf, sizeof(buf), 0);
		if (r <= 0)
			break;
		req.append(buf, size_t(r));
		body_at = req.find("\r\n\r\n");
		if (body_at != std::string::npos)
		{
			// a body (the zip's form): read it all
			const size_t cl = req.find("Content-Length:");
			const size_t want = cl != std::string::npos && cl < body_at ? size_t(atol(req.c_str() + cl + 15)) : 0;
			if (req.size() >= body_at + 4 + std::min<size_t>(want, 200000))
				break;
		}
	}
	if (body_at != std::string::npos)
	{
		const size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
		const std::string method = req.substr(0, sp1);
		std::string target = req.substr(sp1 + 1, sp2 - sp1 - 1), query;
		const size_t q = target.find('?');
		if (q != std::string::npos)
		{
			query = target.substr(q + 1);
			target = target.substr(0, q);
		}
		std::string range;
		const size_t rh = req.find("\nRange:");
		if (rh != std::string::npos && rh < body_at)
		{
			size_t e = req.find('\r', rh);
			range = req.substr(rh + 7, e - rh - 7);
			while (!range.empty() && range[0] == ' ')
				range.erase(0, 1);
		}
		if (method == "GET" && (target == "/" || target == "/index.html"))
			SendText(s, 200, "text/html; charset=utf-8", kPage);
		else if (method == "GET" && target == "/api/list")
		{
			const std::vector<Item> items = Scan();
			std::map<std::string, std::string> games;
			std::string j = "{\"items\":[";
			for (size_t i = 0; i < items.size(); i++)
			{
				const Item& it = items[i];
				games[it.game] = GameName(it.game);
				char row[512];
				snprintf(row, sizeof(row), "%s{\"id\":\"%s\",\"t\":\"%c\",\"g\":\"%s\",\"s\":%lld,\"m\":%lld}", i ? "," : "",
					JsonEscape(it.id).c_str(), it.video ? 'v' : 'p', JsonEscape(it.game).c_str(), it.size, it.mtime);
				j += row;
			}
			j += "],\"games\":{";
			bool first = true;
			for (const auto& g : games)
			{
				j += (first ? "\"" : ",\"") + JsonEscape(g.first) + "\":\"" + JsonEscape(g.second) + "\"";
				first = false;
			}
			j += "}}";
			SendText(s, 200, "application/json", j);
		}
		else if (method == "GET" && (target.rfind("/f/", 0) == 0 || target.rfind("/t/", 0) == 0))
		{
			const std::string id = UrlDecode(target.substr(3));
			if (!SafeId(id))
				SendText(s, 400, "text/plain", "bad name");
			else if (target[1] == 't')
			{
				struct stat st;
				const std::string thumb = ThumbPath(id);
				SendFile(s, stat(thumb.c_str(), &st) == 0 ? thumb : FilePath(id), "", false, "");
			}
			else
			{
				// its MP4 if one was made of it (WebM), else the file itself
				const std::string mp4 = Mp4Path(id);
				std::string nice = NiceName(id);
				if (!mp4.empty() && nice.size() > 5)
					nice = nice.substr(0, nice.size() - 5) + ".mp4";
				SendFile(s, mp4.empty() ? FilePath(id) : mp4, range, query.find("dl=1") != std::string::npos, nice);
			}
		}
		else if (method == "GET" && target.rfind("/icon/", 0) == 0)
		{
			const std::string tid = target.substr(6);
			bool ok = tid.size() >= 4 && tid.size() <= 16;
			for (char c : tid)
				ok = ok && isalnum(uint8_t(c));
			if (ok)
				SendFile(s, "/user/appmeta/" + tid + "/icon0.png", "", false, "");
			else
				SendText(s, 404, "text/plain", "no icon");
		}
		else if (method == "POST" && target == "/zip")
		{
			std::string body = req.substr(body_at + 4);
			const size_t k = body.find("ids=");
			std::vector<std::string> ids;
			if (k != std::string::npos)
			{
				const std::string list = UrlDecode(body.substr(k + 4, body.find('&', k) - k - 4));
				size_t a = 0;
				while (a < list.size())
				{
					size_t b = list.find_first_of("\n,", a);
					if (b == std::string::npos)
						b = list.size();
					std::string id = list.substr(a, b - a);
					while (!id.empty() && (id.back() == '\r' || id.back() == ' '))
						id.pop_back();
					if (!id.empty())
						ids.push_back(id);
					a = b + 1;
				}
			}
			SendZip(s, ids);
		}
		else
			SendText(s, 404, "text/plain", "not found");
	}
	close(s);
	return nullptr;
}

void* Web(void* arg)
{
	const int ls = int(intptr_t(arg));
	for (;;)
	{
		const int c = accept(ls, nullptr, nullptr);
		if (c < 0)
		{
			if (g_listen.load() != ls)
				break; // the app went: the socket was closed
			usleep(100000);
			continue;
		}
		pthread_t t;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, 512 * 1024);
		if (pthread_create(&t, &attr, Client, reinterpret_cast<void*>(intptr_t(c))) == 0)
			pthread_detach(t);
		else
			close(c);
		pthread_attr_destroy(&attr);
	}
	return nullptr;
}

bool StartWeb()
{
	const int s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return false;
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(kWebPort);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 16) != 0)
	{
		OrbisLog("[captures] port %d: %s", kWebPort, strerror(errno));
		close(s);
		return false;
	}
	g_listen = s;
	pthread_t t;
	if (pthread_create(&t, nullptr, Web, reinterpret_cast<void*>(intptr_t(s))) == 0)
		pthread_detach(t);
	OrbisLog("[captures] gallery on port %d", kWebPort);
	return true;
}

void StopWeb()
{
	const int s = g_listen.exchange(-1);
	if (s >= 0)
	{
		shutdown(s, SHUT_RDWR);
		close(s);
		OrbisLog("[captures] gallery stopped (%u file(s) sent)", g_sent.load());
	}
}

// The console's address on the LAN: the source address of a route out (a UDP socket "connected" sends nothing).
std::string LanAddress()
{
	const int s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return "";
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(53);
	inet_pton(AF_INET, "192.168.1.1", &a.sin_addr);
	std::string ip;
	if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0)
	{
		sockaddr_in me = {};
		socklen_t len = sizeof(me);
		if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &len) == 0)
		{
			char buf[32];
			inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf));
			ip = buf;
		}
	}
	close(s);
	return ip == "0.0.0.0" ? "" : ip;
}

void* Control(void*)
{
	const int s = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	sockaddr_in a = {};
	a.sin_family = AF_INET;
	a.sin_port = htons(kControlPort);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (s < 0 || bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 1) != 0)
	{
		OrbisLog("[captures] control port %d: %s (another helper serves it)", kControlPort, strerror(errno));
		if (s >= 0)
			close(s);
		return nullptr;
	}
	for (;;)
	{
		const int c = accept(s, nullptr, nullptr);
		if (c < 0)
		{
			usleep(200000);
			continue;
		}
		setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
		OrbisLog("[captures] the app opened");
		g_sent = 0;
		Scan();
		const bool web = StartWeb();
		int ticks = 0;
		for (;;)
		{
			if (++ticks % 10 == 0)
				Scan(); // every 10 s: new captures counted
			char line[256];
			const std::string ip = LanAddress();
			const int n = snprintf(line, sizeof(line), "IP %s PORT %d PHOTOS %d VIDEOS %d SENT %u WEB %d\n",
				ip.empty() ? "-" : ip.c_str(), kWebPort, g_photos.load(), g_videos.load(), g_sent.load(), web ? 1 : 0);
			if (!SendAll(c, line, size_t(n)))
				break;
			sleep(1);
		}
		close(c);
		StopWeb();
		OrbisLog("[captures] the app closed");
	}
	return nullptr;
}
} // namespace

void StartServer()
{
	pthread_t t;
	if (pthread_create(&t, nullptr, Control, nullptr) == 0)
		pthread_detach(t);
}
} // namespace captures
