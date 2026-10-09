// advertisement — engine-free core: the advertisement.ini model and its reader.
//
// The Metamod plugin (ref/src/Advertisement/advertisement.cpp) read
// addons/configs/advertisement.ini with KeyValues and turned every block of its "list" section
// into up to five messages:
//
//   "text"    -> chat, or the target picked by the legacy numeric "dest" key
//                (1-2 console, 4 hud, 6 centre, anything else chat)
//   "chat"    -> chat (modern spelling, only when "dest" is absent)
//   "center"  -> centre of the screen
//   "hud"     -> the alert line
//   "html"    -> the centre HTML panel
//   "console" -> the console
//
// Line breaks: the config writes them either as an escaped "\n" or as real newlines inside the
// quoted value, and each physical line is trimmed of its indentation before it is shown. The
// KeyValues parser of pisexcompat already turns "\n" into a newline while reading, so SplitLines
// only has to split on real newlines and trim - the old loader's extra "\\n" replacement is kept
// as a no-op safety net for a file that somehow contains a double backslash.
//
// Everything here is free of engine calls so test/advert_core_tests.cpp can drive it against the
// production file (ref/prod-27016/addons/configs/advertisement.ini).

#pragma once

#include <pisex/keyvalues.hpp>

#include <string>
#include <vector>

namespace advert {

// Where one message goes (the five sinks of the Metamod plugin).
enum class Target {
	Chat,
	Center,
	Hud,
	Html,
	Console,
};

inline constexpr float kDefaultInterval = 20.0f;
inline constexpr float kMinimumInterval = 1.0f;
inline constexpr int kHtmlSeconds = 5;

struct Message {
	Target target;
	std::vector<std::string> lines;
};

struct Entry {
	std::vector<Message> messages;
};

struct Config {
	std::vector<Entry> entries;
	float interval = kDefaultInterval;
};

// Splits a config text into trimmed, non-empty lines (escaped "\n" included).
std::vector<std::string> SplitLines(const char* text);
// Joins lines with a separator ("\n" for centre/alert, "<br>" for the HTML panel).
std::string Join(const std::vector<std::string>& lines, const char* separator);
// The legacy "dest" mapping of the original.
Target TargetFromDest(int dest);

// Reads "update time" (clamped to kMinimumInterval) and every entry of "list", in file order,
// duplicate keys included. `root` is the parsed advertisement.ini (the section's contents).
Config ParseConfig(pisex::KeyValues& root, float defaultInterval = kDefaultInterval);

} // namespace advert
