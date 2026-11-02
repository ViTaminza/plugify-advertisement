// advertisement — Plugify port of the Metamod plugin "advertisement":
// periodic server messages from <configs>/advertisement/advertisement.ini.
//
// Every "update time" seconds the next entry of the "list" section is shown to everyone:
// "text" goes to chat (colour tags such as {GREEN}, lines split by \n), "center" to the centre of
// the screen, "hud" to the alert line, "html" to the centre HTML panel and "console" to the
// console. The legacy layout with a numeric "dest" next to "text" still works (1-2 console,
// 3 and 5 chat, 4 hud, 6 centre). The config is re-read on every map start.
//
// Pure s2sdk (docs/PLUGIN_STANDARD.md §7): no pisexcompat and no guard library. The safety rules
// this plugin needs (handoff/weaponskins/PITFALLS.md) live in this file:
//   * B1/B2 map gate: OnMapStart_Register/OnMapEnd_Register in OnPluginStart, unregistered in
//     OnPluginEnd. g_mapReady is latched by OnMapStart and by the first OnPluginUpdate when the
//     listeners are registered and no OnMapEnd was seen (a plugin reloaded into a running map
//     never saw an OnMapStart), and dropped by OnMapEnd. No client/print call is made while the
//     gate is shut: s2sdk's client list is null before the first map and torn down between
//     OnMapEnd and the next OnMapStart.
//   * C9 interval floor: the repeating cadence never goes below one server tick (1/64 s). The
//     first port's pisexcompat CTimer re-armed itself through s2sdk's timer system; here the
//     cadence is driven by OnPluginUpdate(dt) directly (see Tick()).
//   * A7 unload: OnPluginEnd drops the map listeners and the loaded state, so `plg reload` leaves
//     no callback pointing into the unloaded library.
//
// Porting notes:
//   * the engine file system / KeyValues of Metamod and the pisexcompat KeyValues of the first
//     port are replaced by the plugin-local KV1 reader in advert_core.hpp; the file is read from
//     <configs>/advertisement/advertisement.ini, with <configs>/advertisement.ini as a fallback;
//   * the config is read once at plugin start (the interval is needed) and again on every map
//     start, exactly like the original's StartupServer plus MapStartHook;
//   * printing goes through the local printers below (chat is colour-tagged like the original,
//     the other sinks are plain) and only while a map is up - the same guard pisexcompat's
//     output.cpp applied (IsGameReady()), now expressed as g_mapReady;
//   * after a map change the first message is shown on the first ready update. The previous port
//     left its repeating timer registered across the changelevel, so a fire in that window was
//     parked and re-armed one tick after the next map start (pisexcompat timers.cpp); OnMapEnd
//     sets the same "fire on the next ready frame" flag here.

#include "advert_core.hpp"
#include "chat_text.hpp"

#include <plg/plugin.hpp>
#include <plg/string.hpp>
#include <plugin_export.h>

#include <s2sdk/s2sdk.hpp>

#include <array>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr char kLogTag[] = "advertisement";

// CS2 has 64 player slots; the per-slot print fallback iterates them like the original.
constexpr int32_t kMaxSlots = 64;

// PITFALLS C9: the shortest repeating delay that cannot re-fire inside one engine frame. s2sdk's
// timer system would loop forever on a stored 0, so even a broken config value must be floored.
constexpr float kMinimumFireInterval = 1.0f / 64.0f;

std::vector<advert::Entry> g_entries;
float g_interval = advert::kDefaultInterval;
std::size_t g_next = 0;

// --- map gate (PITFALLS B1/B2) ----------------------------------------------------------------

bool g_mapReady = false;      // s2sdk client/print calls are safe right now
bool g_mapStartSeen = false;  // an OnMapStart arrived since OnPluginStart / the last OnMapEnd
bool g_mapEndSeen = false;    // an OnMapEnd arrived since OnPluginStart
bool g_mapListenersRegistered = false;

// --- the repeating cadence --------------------------------------------------------------------

// Seconds accumulated since the last shown entry, counted only while a map is up.
float g_elapsed = 0.0f;
// A message is due on the first ready update: the previous port's timer was parked by its
// map-change guard (pisexcompat timers.cpp) and re-armed one tick after the next map start.
bool g_fireOnReady = false;

// --- logs -------------------------------------------------------------------------------------

// Tagged logs (the format is checked by the compiler).
void LogMessage(plg::Severity severity, std::string_view message) {
	if (!plg::Log) {
		return;
	}
	try {
		plg::Log(std::string("[") + kLogTag + "] " + std::string(message), severity,
				 plg::source_location::current());
	} catch (...) {
	}
}

void LogError(const char* format, ...) __attribute__((format(printf, 1, 2)));
void LogError(const char* format, ...) {
	std::array<char, 2048> buffer{};
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer.data(), buffer.size(), format, args);
	va_end(args);
	LogMessage(plg::Severity::Error, buffer.data());
}

void LogInfo(const char* format, ...) __attribute__((format(printf, 1, 2)));
void LogInfo(const char* format, ...) {
	std::array<char, 2048> buffer{};
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer.data(), buffer.size(), format, args);
	va_end(args);
	LogMessage(plg::Severity::Info, buffer.data());
}

// <plg::GetConfigsDir()>; empty when the plugify host exposes no configs directory (tests).
std::string ConfigsDir() {
	try {
		return plg::GetConfigsDir ? std::string(plg::GetConfigsDir()) : std::string();
	} catch (...) {
		return {};
	}
}

// Reads (or re-reads) the configuration; keeps the current entry index when possible. A missing
// or malformed file leaves the plugin with no entries and the default interval, exactly like the
// original's LoadConfig on a failed KeyValues load (fail soft, PITFALLS A1).
void LoadConfig() {
	advert::Config config;
	const std::string configsDir = ConfigsDir();
	if (configsDir.empty()) {
		LogError("the plugify configs directory is unavailable - no advertisement is shown");
	} else {
		std::string path;
		std::optional<advert::Config> loaded = advert::LoadConfigFromDir(configsDir, &path);
		if (!loaded) {
			if (path.empty()) {
				LogError("Failed to load %s: no such file under %s",
						 std::string(advert::kConfigFile).c_str(), configsDir.c_str());
			} else {
				LogError("Failed to load %s", path.c_str());
			}
		} else {
			config = std::move(*loaded);
			LogInfo("loaded %zu entries from %s, update time %.1fs", config.entries.size(),
					path.c_str(), static_cast<double>(config.interval));
		}
	}
	g_entries = std::move(config.entries);
	g_interval = config.interval;
	if (g_next >= g_entries.size()) {
		g_next = 0;
	}
}

// --- output (only while a map is up, matching libs/pisexcompat/src/output.cpp) -----------------

// The original's chat sink: every line went through PrintToChatAll(" %s", line), which colour
// tagged the text. s2sdk::PrintToChatAll is the direct binding; when it is unavailable the same
// line is sent per slot through s2sdk::PrintToChat (slot range only, which is the guarantee
// PrintToChatAll itself relies on while a map exists).
void PrintToChatAll(std::string_view text) {
	if (!g_mapReady) {
		return;
	}
	const std::string colorized = advert::Colorize(text);
	if (__s2sdk_PrintToChatAll != nullptr) {
		try {
			s2sdk::PrintToChatAll(plg::string(colorized));
		} catch (const std::exception& ex) {
			LogError("PrintToChatAll failed: %s", ex.what());
		} catch (...) {
			LogError("PrintToChatAll failed with an unknown exception");
		}
		return;
	}
	if (__s2sdk_PrintToChat == nullptr) {
		return;
	}
	for (int32_t slot = 0; slot < kMaxSlots; ++slot) {
		try {
			s2sdk::PrintToChat(slot, plg::string(colorized));
		} catch (...) {
			// One slot must not stop the rest of the line.
		}
	}
}

void PrintToCenterAll(std::string_view text) {
	if (!g_mapReady || __s2sdk_PrintCenterTextAll == nullptr) {
		return;
	}
	try {
		s2sdk::PrintCenterTextAll(plg::string(text));
	} catch (...) {
		LogError("PrintCenterTextAll failed");
	}
}

void PrintToAlertAll(std::string_view text) {
	if (!g_mapReady || __s2sdk_PrintAlertTextAll == nullptr) {
		return;
	}
	try {
		s2sdk::PrintAlertTextAll(plg::string(text));
	} catch (...) {
		LogError("PrintAlertTextAll failed");
	}
}

void PrintToCenterHtmlAll(int32_t duration, std::string_view text) {
	if (!g_mapReady || __s2sdk_PrintCentreHtmlAll == nullptr) {
		return;
	}
	try {
		s2sdk::PrintCentreHtmlAll(plg::string(text), duration);
	} catch (...) {
		LogError("PrintCentreHtmlAll failed");
	}
}

void PrintToConsoleAll(std::string_view text) {
	if (!g_mapReady || __s2sdk_PrintToConsoleAll == nullptr) {
		return;
	}
	try {
		s2sdk::PrintToConsoleAll(plg::string(text));
	} catch (...) {
		LogError("PrintToConsoleAll failed");
	}
}

// The text always goes in as an argument and is never used as a printf format: a "%" in an ad must
// not be read as a conversion.
void Show(const advert::Message& message) {
	switch (message.target) {
		case advert::Target::Chat:
			for (const std::string& line : message.lines) {
				// A leading space keeps a colour tag at the very start working in CS2 chat.
				PrintToChatAll(" " + line);
			}
			break;
		case advert::Target::Center:
			PrintToCenterAll(advert::Join(message.lines, "\n"));
			break;
		case advert::Target::Hud:
			PrintToAlertAll(advert::Join(message.lines, "\n"));
			break;
		case advert::Target::Html:
			PrintToCenterHtmlAll(advert::kHtmlSeconds, advert::Join(message.lines, "<br>"));
			break;
		case advert::Target::Console:
			for (const std::string& line : message.lines) {
				PrintToConsoleAll(line + "\n");
			}
			break;
	}
}

// Shows the next entry and advances the rotation (the original's OnTimer body).
void ShowNext() {
	if (g_entries.empty()) {
		return;
	}
	if (g_next >= g_entries.size()) {
		g_next = 0;
	}
	for (const advert::Message& message : g_entries[g_next].messages) {
		Show(message);
	}
	g_next = (g_next + 1) % g_entries.size();
}

// The interval with the PITFALLS C9 floor. NaN fails the comparison on purpose: a broken config
// value means "one tick", never a zero delay.
float EffectiveInterval() {
	return g_interval > kMinimumFireInterval ? g_interval : kMinimumFireInterval;
}

// Drives the repeating advertisement interval from OnPluginUpdate. The delay is counted only while
// a map is up, and at most one entry is shown per update - the old repeating timer fired once per
// frame at most and re-armed itself from the moment its callback returned.
void Tick(float dtSeconds) {
	if (!g_mapReady) {
		return;
	}
	if (g_fireOnReady) {
		g_fireOnReady = false;
		g_elapsed = 0.0f;
		ShowNext();
		return;
	}
	if (!(dtSeconds > 0.0f)) {
		return; // a zero, negative or NaN update advances nothing
	}
	g_elapsed += dtSeconds;
	if (g_elapsed >= EffectiveInterval()) {
		g_elapsed = 0.0f;
		ShowNext();
	}
}

// --- map gate (PITFALLS B1/B2) ----------------------------------------------------------------

// s2sdk listeners: plain function pointers called on the game thread. They only change state -
// and reload the config on a map start, which is a file read, not a client call.
void OnMapStartListener() {
	g_mapReady = true;
	g_mapStartSeen = true;
	g_mapEndSeen = false;
	// MapStartHook of the original: the config is re-read on every map start.
	try {
		LoadConfig();
	} catch (const std::exception& ex) {
		LogError("reloading the config on map start failed: %s", ex.what());
	} catch (...) {
		LogError("reloading the config on map start failed with an unknown exception");
	}
}

void OnMapEndListener() {
	// PITFALLS B2: from here until the next OnMapStart s2sdk's client list is torn down, so this
	// handler drops state and makes no client call.
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = true;
	g_elapsed = 0.0f;
	// The old pisexcompat timer stayed registered across the changelevel: a fire in the window
	// was parked and re-armed one tick after the next map start (timers.cpp). Mirror that so the
	// first message of the new map is not delayed by a whole interval.
	g_fireOnReady = true;
}

void InitMapGate() {
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
	g_elapsed = 0.0f;
	g_fireOnReady = false;
	if (g_mapListenersRegistered) {
		return;
	}
	if (__s2sdk_OnMapStart_Register == nullptr || __s2sdk_OnMapEnd_Register == nullptr) {
		// Without the map-end listener nothing could ever take readiness away again, so the gate
		// stays closed for good (PITFALLS B2: log and stay inactive, never return Failed).
		LogError("the s2sdk map listeners are unavailable - no advertisement will be shown");
		return;
	}
	s2sdk::OnMapStart_Register(&OnMapStartListener);
	s2sdk::OnMapEnd_Register(&OnMapEndListener);
	g_mapListenersRegistered = true;
}

void PollMapGate() {
	if (!g_mapListenersRegistered) {
		return;
	}
	// A plugin loaded into a running map never saw an OnMapStart: the first update after
	// OnPluginStart is the proof of a live map (the host pumps updates from the server frame).
	// After an OnMapEnd only a new OnMapStart re-opens the gate: the host keeps pumping updates
	// through a changelevel, so `!g_mapEndSeen` must not be enough once one was seen.
	if (!g_mapReady && (g_mapStartSeen || !g_mapEndSeen)) {
		g_mapReady = true;
		g_mapStartSeen = true;
	}
}

void ShutdownMapGate() {
	if (g_mapListenersRegistered) {
		g_mapListenersRegistered = false;
		if (__s2sdk_OnMapStart_Unregister != nullptr) {
			s2sdk::OnMapStart_Unregister(&OnMapStartListener);
		}
		if (__s2sdk_OnMapEnd_Unregister != nullptr) {
			s2sdk::OnMapEnd_Unregister(&OnMapEndListener);
		}
	}
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
	g_elapsed = 0.0f;
	g_fireOnReady = false;
}

class AdvertisementPlugin final : public plg::Plugin {
public:
	plg::PluginResult OnPluginStart() override {
		try {
			// The map listeners come first: they gate every s2sdk call this plugin makes (B1/B2).
			InitMapGate();
			// Reads a file only - legal before the first map.
			LoadConfig();
			LogInfo("ready: %zu entries, update time %.1fs", g_entries.size(),
					static_cast<double>(g_interval));
		} catch (const std::exception& ex) {
			LogError("OnPluginStart failed: %s", ex.what());
		} catch (...) {
			LogError("OnPluginStart failed with an unknown exception");
		}
		return {}; // never Failed: that stops every plugify plugin (PITFALLS A1)
	}

	plg::PluginResult OnPluginUpdate(std::chrono::milliseconds dt) override {
		try {
			// A plugin loaded into a running map: the first update latches readiness (a no-op when
			// a map is already up, never after an OnMapEnd).
			PollMapGate();
			Tick(static_cast<float>(dt.count()) / 1000.0f);
		} catch (const std::exception& ex) {
			LogError("update failed: %s", ex.what());
		} catch (...) {
			LogError("update failed with an unknown exception");
		}
		return {};
	}

	plg::PluginResult OnPluginEnd() override {
		try {
			// Symmetrical teardown (PITFALLS A7): nothing may point into this library afterwards.
			ShutdownMapGate();
			g_entries.clear();
			g_interval = advert::kDefaultInterval;
			g_next = 0;
		} catch (...) {
			// nothing left to do at unload
		}
		return {};
	}
};

AdvertisementPlugin g_advertisementPlugin;

} // namespace

PLUGIFY_PLUGIN(PLUGIN_API, &g_advertisementPlugin)
