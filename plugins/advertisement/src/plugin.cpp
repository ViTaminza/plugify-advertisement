// advertisement — Plugify port of the Metamod plugin "advertisement" (ref/src/Advertisement):
// periodic server messages from <configs>/advertisement/advertisement.ini.
//
// Every "update time" seconds the next entry of the "list" section is shown to everyone:
// "text" goes to chat (colour tags such as {GREEN}, lines split by \n), "center" to the centre of
// the screen, "hud" to the alert line, "html" to the centre HTML panel and "console" to the
// console. The legacy layout with a numeric "dest" next to "text" still works (1-2 console,
// 3 and 5 chat, 4 hud, 6 centre). The config is re-read on every map start.
//
// Porting notes:
//   * the engine file system / KeyValues of Metamod are replaced by pisex::KeyValues (the same
//     KV1 parser the other ported plugins use); the path keeps the Metamod spelling
//     "addons/configs/advertisement.ini" and resolves to <configs>/advertisement/advertisement.ini;
//   * printing goes through pisexcompat's printers (chat is colour-tagged like the original,
//     the others are plain), which are no-ops before a map exists - the original's
//     gpGlobals/entity-system guard is therefore unnecessary;
//   * the timer is a pisexcompat CTimer; its callback returns the delay until the next run
//     (the current "update time"), exactly like the Pisex timer it replaces, so a config reload
//     changes the cadence from the next message onwards;
//   * StartupServer (Metamod) plus the "config is re-read on every map start" comment of the
//     original map to MapStartHook here: the file is loaded once at plugin start (the timer needs
//     its interval) and again on every map start.

#include "advert_core.hpp"

#include <pisex/pisex.hpp>

#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>

#include <array>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr char kLogTag[] = "advertisement";
// The Metamod spelling; pisexcompat resolves it to <configs>/advertisement/advertisement.ini.
constexpr char kConfigPath[] = "addons/configs/advertisement.ini";

std::vector<advert::Entry> g_entries;
float g_interval = advert::kDefaultInterval;
std::size_t g_next = 0;
pisex::CTimer* g_timer = nullptr;

// Tagged logs (the format is checked by the compiler).
void LogError(const char* format, ...) __attribute__((format(printf, 1, 2)));
void LogError(const char* format, ...) {
	std::array<char, 2048> buffer{};
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer.data(), buffer.size(), format, args);
	va_end(args);
	pisex::ErrorLog("[%s] %s", kLogTag, buffer.data());
}

void LogInfo(const char* format, ...) __attribute__((format(printf, 1, 2)));
void LogInfo(const char* format, ...) {
	std::array<char, 2048> buffer{};
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer.data(), buffer.size(), format, args);
	va_end(args);
	if (plg::Log) {
		plg::Log(std::string_view(buffer.data()), plg::Severity::Info, plg::source_location::current());
	}
}

// Reads (or re-reads) the configuration; keeps the current entry index when possible.
void LoadConfig() {
	advert::Config config;
	pisex::KeyValues::AutoDelete kv("advertisement");
	if (!kv->LoadFromFile(kConfigPath)) {
		LogError("Failed to load %s", kConfigPath);
	} else {
		config = advert::ParseConfig(*kv);
	}
	g_entries = std::move(config.entries);
	g_interval = config.interval;
	if (g_next >= g_entries.size()) {
		g_next = 0;
	}
}

// The text always goes in as an argument: a "%" in an ad must not be read as a format.
void Show(const advert::Message& message) {
	switch (message.target) {
		case advert::Target::Chat:
			for (const std::string& line : message.lines) {
				// A leading space keeps a colour tag at the very start working in CS2 chat.
				pisex::PrintToChatAll(" %s", line.c_str());
			}
			break;
		case advert::Target::Center:
			pisex::PrintToCenterAll("%s", advert::Join(message.lines, "\n").c_str());
			break;
		case advert::Target::Hud:
			pisex::PrintToAlertAll("%s", advert::Join(message.lines, "\n").c_str());
			break;
		case advert::Target::Html:
			pisex::PrintToCenterHtmlAll(advert::kHtmlSeconds, "%s", advert::Join(message.lines, "<br>").c_str());
			break;
		case advert::Target::Console:
			for (const std::string& line : message.lines) {
				pisex::PrintToConsoleAll("%s\n", line.c_str());
			}
			break;
	}
}

float OnTimer() {
	// Between maps there are no players to print to (the printers are no-ops then anyway).
	if (g_entries.empty()) {
		return g_interval;
	}
	if (g_next >= g_entries.size()) {
		g_next = 0;
	}
	for (const advert::Message& message : g_entries[g_next].messages) {
		Show(message);
	}
	g_next = (g_next + 1) % g_entries.size();
	return g_interval;
}

class AdvertisementPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override {
		try {
			// Never Failed: a plugin that fails OnPluginStart stops every Plugify plugin.
			pisex::Init("advertisement"); // <configs>/advertisement (+ translations)
			LoadConfig();
			g_timer = pisex::CreateTimer(g_interval, OnTimer);
			if (!g_timer) {
				LogError("could not create the advertisement timer");
			}
			// The config is re-read on every map start (the original did the same).
			pisex::g_pUtils->MapStartHook([](const char*) { LoadConfig(); });
			LogInfo("loaded %zu entries, update time %.1fs", g_entries.size(), static_cast<double>(g_interval));
			return plg::PluginResult{};
		} catch (const std::exception& ex) {
			LogError("OnPluginStart failed: %s", ex.what());
			return plg::PluginResult{};
		} catch (...) {
			LogError("OnPluginStart failed with an unknown exception");
			return plg::PluginResult{};
		}
	}

	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds) override {
		try {
			pisex::Poll();
		} catch (const std::exception& ex) {
			LogError("update failed: %s", ex.what());
		} catch (...) {
			LogError("update failed with an unknown exception");
		}
		return plg::PluginResult{};
	}

	plg::PluginResult OnPluginEnd() override {
		try {
			if (g_timer) {
				pisex::RemoveTimer(g_timer);
				g_timer = nullptr;
			}
			pisex::Shutdown();
		} catch (...) {
		}
		return plg::PluginResult{};
	}
};

AdvertisementPlugin g_advertisementPlugin;

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_advertisementPlugin)
