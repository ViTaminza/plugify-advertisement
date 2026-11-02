// advertisement — engine-free core: the advertisement.ini model, its reader and its locations.
//
// The Metamod plugin read addons/configs/advertisement.ini with KeyValues and turned every block
// of its "list" section into up to five messages:
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
// quoted value, and each physical line is trimmed of its indentation before it is shown. The KV1
// parser below turns "\n" into a newline while reading, so SplitLines only has to split on real
// newlines and trim - the old loader's extra "\\n" replacement is kept as a no-op safety net for a
// file that somehow contains a double backslash.
//
// The plugin is pure s2sdk (docs/PLUGIN_STANDARD.md §7), so the hl2sdk/pisexcompat KeyValues the
// first port used is replaced by the small reader in this file - it lives in the plugin itself and
// has no engine or library dependency. Its grammar is the KV1 subset every Pisex config uses:
//
//   * quoted ("key" "value") and unquoted (key value) tokens, nested "key" { ... } blocks, and a
//     key may carry both a value and a block (Valve's KV1),
//   * // line comments and /* */ block comments, escapes \" \\ \n \r \t inside quoted strings,
//   * a UTF-8 BOM at the start of the file is ignored,
//   * #base / #include / #define lines are skipped (no file inclusion, like the old loader),
//   * malformed input is rejected as a whole (std::nullopt): an unterminated quoted string, a
//     missing '}', a stray '}' or '{', or an unterminated block comment. A caller that gets
//     nullopt keeps its previous/default configuration instead of a half-read tree.
//
// Like Valve's loader, a file that is a single "Root" { ... } block is returned as that block, so
// `ParseKv1(text)->GetString("key")` reads a key *inside* it. Key lookup is case-insensitive.
//
// Everything here is free of engine calls so tests/advertisement_tests.cpp can drive it against
// the advertisement.ini the package ships.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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

// The config locations relative to <GetConfigsDir()>: the plugin's own folder
// (PLUGIN_STANDARD §3 puts a plugin's config in <configs>/<name>/) and the legacy flat layout.
inline constexpr std::string_view kPluginDir = "advertisement";
inline constexpr std::string_view kConfigFile = "advertisement.ini";

// --- KV1 reader ---------------------------------------------------------------------------------

// One node of a parsed file: a key, its optional scalar value and its nested sub-keys. Insertion
// order is kept, so duplicate keys (the production file repeats "5") stay in file order.
struct KvNode {
	std::string name;
	std::string value;
	bool hasValue = false;
	std::vector<KvNode> children;

	// True when the node carries a nested block. pisexcompat's KeyValues called this a "true sub
	// key"; a node with no children is a plain value.
	bool IsBlock() const { return !children.empty(); }

	// First child with that name (case-insensitive), or nullptr.
	const KvNode* Find(std::string_view key) const;
	// The child's value; `def` when the child is missing or has no value.
	std::string_view GetString(std::string_view key, std::string_view def = {}) const;
	// The child's value as an int, with Valve's atoi fallback ("5 // x" -> 5, "abc" -> 0); `def`
	// when the child is missing or its value is empty.
	int GetInt(std::string_view key, int def = 0) const;
	// The child's value as a float (strtof semantics); `def` when the child is missing, its value
	// is empty or it does not start with a number.
	float GetFloat(std::string_view key, float def = 0.0f) const;
};

// Parses an in-memory KV1 document. std::nullopt when the text is malformed (see the grammar in
// the file comment); a single top-level block is unwrapped into the returned root.
std::optional<KvNode> ParseKv1(std::string_view text);
// Same over a file; std::nullopt when the file cannot be opened or its text is malformed.
std::optional<KvNode> LoadKv1File(const std::string& path);

// --- the advertisement.ini model ----------------------------------------------------------------

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

// Splits a config text into trimmed, non-empty lines (a literal "\n" escape included).
std::vector<std::string> SplitLines(std::string_view text);
// Joins lines with a separator ("\n" for centre/alert, "<br>" for the HTML panel).
std::string Join(const std::vector<std::string>& lines, std::string_view separator);
// The legacy "dest" mapping of the original.
Target TargetFromDest(int dest);

// Reads "update time" (clamped to kMinimumInterval) and every entry of "list", in file order,
// duplicate keys included. `root` is the parsed advertisement.ini (the section's contents).
Config ParseConfig(const KvNode& root, float defaultInterval = kDefaultInterval);

// --- where the file is read from ----------------------------------------------------------------

// `base`/`name` with exactly one '/' between them (empty on either side handled).
std::string JoinPath(std::string_view base, std::string_view name);
bool FileExists(const std::string& path);

// <configsDir>/advertisement/advertisement.ini, then <configsDir>/advertisement.ini. Empty when
// no configs directory is available.
std::vector<std::string> ConfigCandidates(std::string_view configsDir);

// Parse a document / load one file into the model; std::nullopt when it cannot be read or parsed.
std::optional<Config> LoadConfigFromText(std::string_view text);
std::optional<Config> LoadConfigFromFile(const std::string& path);

// Tries the candidates in order and reads the first one that exists. When that file is malformed
// the result is std::nullopt (the next candidate is not tried: a broken primary must not silently
// fall back to a stale copy). `usedPath`, when non-null, receives the file that was read; empty
// when no candidate exists.
std::optional<Config> LoadConfigFromDir(std::string_view configsDir, std::string* usedPath = nullptr);

} // namespace advert
