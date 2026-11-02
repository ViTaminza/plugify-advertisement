// advertisement_tests — the engine-free test binary of the pure-s2sdk advertisement plugin.
//
// The plugin TU is compiled into this test (like resetscore_tests/as_rcon_tests), so the whole
// plugin code path is exercised; every __s2sdk_* binding slot is a recording fake. Covered:
//
//   1. the plugin-local KV1 reader (advert_core.hpp): quoted/unquoted tokens, // and /* */
//      comments, escapes, a UTF-8 BOM, nested subkeys, # directives, case-insensitive lookup,
//      GetInt/GetFloat/GetString semantics and every malformed shape (nullopt);
//   2. the shipped advertisement.ini (tests/data, byte-identical to configs/advertisement.ini
//      apart from the example.com domains): the interval and all nine entries in file order,
//      duplicate keys included;
//   3. the message model in memory: the legacy "dest", the named sinks, the secondary "chat" key,
//      empty/blank texts, the interval clamp and the defaults of a partial file;
//   4. SplitLines()/Join() and the cs2-menus Colorize() tag table byte-for-byte;
//   5. the config locations: <configs>/advertisement/advertisement.ini first, then
//      <configs>/advertisement.ini, a malformed primary not silently replaced by the fallback;
//   6. PITFALLS B1/B2 — the map gate: not one print call before the first map, in the window
//      between OnMapEnd and the next OnMapStart, or after OnPluginEnd; the first update latches
//      readiness only when no OnMapEnd was seen; without the map listeners the gate never opens;
//      OnMapStart re-reads the config (MapStartHook) and the message parked by the map change is
//      shown on the first ready update;
//   7. the update-driven interval (PITFALLS C9): the 1/64 s floor for zero, negative and NaN
//      values, at most one message per update, and the rotation (incl. wrap-around);
//   8. the five output sinks: chat with Colorize and the leading space, center, alert, centre-HTML
//      with the 5 s duration, console lines with their trailing newline, and the per-slot
//      PrintToChat fallback when PrintToChatAll is unavailable;
//   9. missing bindings (prints, configs dir) stay soft (PITFALLS A1) and a clean unload (A7)
//      removes the map listeners and drops the loaded state.
//
// No pisexcompat, no Plugify host, no engine. Run by ./test.sh (build/tests/advertisement_tests).

#include "../src/plugin.cpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Section(const char* name) {
	std::printf("-- %s\n", name);
}

void Check(bool condition, const std::string& what) {
	++g_checks;
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

void CheckEqInt(int64_t actual, int64_t expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected %lld, got %lld)\n", what.c_str(), static_cast<long long>(expected),
					static_cast<long long>(actual));
	}
}

void CheckEqStr(std::string_view actual, std::string_view expected, const std::string& what) {
	++g_checks;
	if (actual != expected) {
		++g_failures;
		std::printf("FAIL: %s (expected '%s', got '%s')\n", what.c_str(), std::string(expected).c_str(),
					std::string(actual).c_str());
	}
}

void CheckEqFloat(float actual, float expected, const std::string& what) {
	++g_checks;
	if (!(actual == expected)) {
		++g_failures;
		std::printf("FAIL: %s (expected %f, got %f)\n", what.c_str(), static_cast<double>(expected),
					static_cast<double>(actual));
	}
}

std::string ToStd(const plg::string& value) {
	return std::string(std::string_view(value));
}

// --- the fake Plugify host -------------------------------------------------------------------

std::vector<std::string> g_logLines;
std::vector<plg::Severity> g_logSeverities;

void FakeLog(std::string_view message, plg::Severity severity, const plg::source_location& /*location*/) {
	g_logLines.emplace_back(message);
	g_logSeverities.push_back(severity);
}

bool HasLogContaining(std::string_view needle) {
	for (const std::string& line : g_logLines) {
		if (line.find(needle) != std::string::npos) {
			return true;
		}
	}
	return false;
}

bool HasErrorLog() {
	for (const plg::Severity severity : g_logSeverities) {
		if (severity == plg::Severity::Error) {
			return true;
		}
	}
	return false;
}

std::string g_configsDir;

plg::string FakeGetConfigsDir() {
	return plg::string(g_configsDir);
}

// --- the fake s2sdk bindings -----------------------------------------------------------------

std::vector<std::string> g_chatAll;
std::vector<std::string> g_chatSlotTexts;
std::vector<int32_t> g_chatSlotSlots;
std::vector<std::string> g_center;
std::vector<std::string> g_alert;
std::vector<std::string> g_html;
std::vector<int32_t> g_htmlDurations;
std::vector<std::string> g_console;

void FakePrintToChatAll(const plg::string& text) {
	g_chatAll.push_back(ToStd(text));
}

void FakePrintToChat(int32_t slot, const plg::string& text) {
	g_chatSlotSlots.push_back(slot);
	g_chatSlotTexts.push_back(ToStd(text));
}

void FakePrintCenterTextAll(const plg::string& text) {
	g_center.push_back(ToStd(text));
}

void FakePrintAlertTextAll(const plg::string& text) {
	g_alert.push_back(ToStd(text));
}

void FakePrintCentreHtmlAll(const plg::string& text, int32_t duration) {
	g_html.push_back(ToStd(text));
	g_htmlDurations.push_back(duration);
}

void FakePrintToConsoleAll(const plg::string& text) {
	g_console.push_back(ToStd(text));
}

s2sdk::OnMapStartCallback g_mapStartCallback = nullptr;
s2sdk::OnMapEndCallback g_mapEndCallback = nullptr;
int g_mapStartRegistrations = 0;
int g_mapStartUnregistrations = 0;
int g_mapEndRegistrations = 0;
int g_mapEndUnregistrations = 0;

void FakeOnMapStartRegister(s2sdk::OnMapStartCallback callback) {
	g_mapStartCallback = callback;
	++g_mapStartRegistrations;
}

void FakeOnMapStartUnregister(s2sdk::OnMapStartCallback callback) {
	if (g_mapStartCallback == callback) {
		g_mapStartCallback = nullptr;
	}
	++g_mapStartUnregistrations;
}

void FakeOnMapEndRegister(s2sdk::OnMapEndCallback callback) {
	g_mapEndCallback = callback;
	++g_mapEndRegistrations;
}

void FakeOnMapEndUnregister(s2sdk::OnMapEndCallback callback) {
	if (g_mapEndCallback == callback) {
		g_mapEndCallback = nullptr;
	}
	++g_mapEndUnregistrations;
}

void FireMapStart() {
	if (g_mapStartCallback != nullptr) {
		g_mapStartCallback();
	}
}

void FireMapEnd() {
	if (g_mapEndCallback != nullptr) {
		g_mapEndCallback();
	}
}

void InstallFakes() {
	plg::Log = &FakeLog;
	plg::GetConfigsDir = &FakeGetConfigsDir;

	__s2sdk_OnMapStart_Register = &FakeOnMapStartRegister;
	__s2sdk_OnMapStart_Unregister = &FakeOnMapStartUnregister;
	__s2sdk_OnMapEnd_Register = &FakeOnMapEndRegister;
	__s2sdk_OnMapEnd_Unregister = &FakeOnMapEndUnregister;

	__s2sdk_PrintToChatAll = &FakePrintToChatAll;
	__s2sdk_PrintToChat = &FakePrintToChat;
	__s2sdk_PrintCenterTextAll = &FakePrintCenterTextAll;
	__s2sdk_PrintAlertTextAll = &FakePrintAlertTextAll;
	__s2sdk_PrintCentreHtmlAll = &FakePrintCentreHtmlAll;
	__s2sdk_PrintToConsoleAll = &FakePrintToConsoleAll;
}

void ResetOutputs() {
	g_chatAll.clear();
	g_chatSlotTexts.clear();
	g_chatSlotSlots.clear();
	g_center.clear();
	g_alert.clear();
	g_html.clear();
	g_htmlDurations.clear();
	g_console.clear();
}

bool HasAnyOutput() {
	return !g_chatAll.empty() || !g_chatSlotTexts.empty() || !g_center.empty() || !g_alert.empty() ||
		   !g_html.empty() || !g_console.empty();
}

void CheckNoOutput(const std::string& what) {
	Check(!HasAnyOutput(), what);
}

// --- the temp config directory ----------------------------------------------------------------

std::filesystem::path TempRoot() {
	return std::filesystem::temp_directory_path() / "advertisement_tests_work";
}

std::string PathString(const std::filesystem::path& path) {
	return path.string();
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(content.data(), static_cast<std::streamsize>(content.size()));
	const bool ok = file.good();
	file.close();
	Check(ok, "write " + path.string());
}

// <temp>/advertisement/advertisement.ini — the primary location the plugin reads.
void WriteAppConfig(std::string_view text) {
	WriteFile(TempRoot() / "advertisement" / "advertisement.ini", text);
}

// The shipped sample. CMake bakes in the absolute source path, which is /src/... when the tree is
// configured inside the builder container; when the binary is started on the host instead, the
// path relative to build/tests (the working directory test.sh uses) still points at the same file.
std::string ShippedConfigPath() {
	const std::string configured = ADVERTISEMENT_TEST_CONFIG;
	if (advert::FileExists(configured)) {
		return configured;
	}
	// Relative to build/tests (test.sh) and to the repository root (a manual ./build/tests/...
	// invocation); the normalised path is only used when it really exists.
	const std::filesystem::path relativeToTests =
			std::filesystem::current_path() / "../../plugins/advertisement/tests/data/advertisement.ini";
	const std::filesystem::path relativeToRoot =
			std::filesystem::current_path() / "plugins/advertisement/tests/data/advertisement.ini";
	for (const std::filesystem::path& candidate : {relativeToTests, relativeToRoot}) {
		const std::string path = PathString(candidate.lexically_normal());
		if (advert::FileExists(path)) {
			return path;
		}
	}
	return configured;
}

// The config the output tests rotate through: one entry per sink, five in total.
constexpr std::string_view kSinksConfig = R"(
"advertisement"
{
	"update time" "1"

	"list"
	{
		"1" { "text" "first line\nsecond line" }
		"2" { "center" "centered text" }
		"3" { "hud" "alert text" }
		"4" { "html" "html one\nhtml two" }
		"5" { "console" "console one\nconsole two" }
	}
}
)";

// Two single-line chat entries: easier to count than the sink config.
constexpr std::string_view kAbConfig =
		R"("advertisement" { "update time" "1" "list" { "1" { "text" "alpha" } "2" { "text" "beta" } } })";

// A fresh fake host and a fresh plugin state, exactly as a plugin the host has not started yet.
void ResetAll() {
	InstallFakes();
	ResetOutputs();
	g_logLines.clear();
	g_logSeverities.clear();

	g_mapStartCallback = nullptr;
	g_mapEndCallback = nullptr;
	g_mapStartRegistrations = 0;
	g_mapStartUnregistrations = 0;
	g_mapEndRegistrations = 0;
	g_mapEndUnregistrations = 0;

	g_entries.clear();
	g_interval = advert::kDefaultInterval;
	g_next = 0;
	g_mapReady = false;
	g_mapStartSeen = false;
	g_mapEndSeen = false;
	g_mapListenersRegistered = false;
	g_elapsed = 0.0f;
	g_fireOnReady = false;

	std::error_code ec;
	std::filesystem::remove_all(TempRoot(), ec);
	std::filesystem::create_directories(TempRoot(), ec);
	g_configsDir = PathString(TempRoot());
}

AdvertisementPlugin& g_plugin = g_advertisementPlugin;

void StartPlugin(std::string_view config = kSinksConfig) {
	ResetAll();
	WriteAppConfig(config);
	Check(static_cast<bool>(g_plugin.OnPluginStart()), "OnPluginStart never returns Failed");
}

void Advance(int64_t milliseconds) {
	g_plugin.OnPluginUpdate(std::chrono::milliseconds(milliseconds));
}

// ==============================================================================================
// 1. the KV1 reader
// ==============================================================================================

void TestKv1Reader() {
	Section("KV1 reader: syntax");

	const std::optional<advert::KvNode> root = advert::ParseKv1(R"(
// a line comment before the root
"root"
{
	/* a block
	   comment */
	"quoted"  "value"   // a trailing comment
	unquoted  bare
	"escapes" "a\tb\nc\"d\\e\q"
	"empty"   ""
	"block"
	{
		"nested" "inner"
		"block" { "deep" "42" }
	}
	"both" "value" { "child" "x" }
	"dup" "1"
	"dup" "2"
	"#" not-a-directive
}
)");
	Check(root.has_value(), "a well-formed document parses");
	if (!root) {
		return;
	}
	CheckEqStr(root->name, "root", "the single top-level block is the root");
	CheckEqStr(root->GetString("quoted"), "value", "a quoted value");
	CheckEqStr(root->GetString("QUOTED"), "value", "key lookup is case-insensitive");
	CheckEqStr(root->GetString("unquoted"), "bare", "an unquoted value");
	CheckEqStr(root->GetString("escapes"), "a\tb\nc\"d\\e\\q",
			   "\\t, \\n, \\\", \\\\ are decoded and an unknown escape keeps its backslash");
	CheckEqStr(root->GetString("empty"), "", "an empty quoted value");
	CheckEqStr(root->GetString("missing"), "", "a missing key answers the empty default");
	CheckEqStr(root->GetString("missing", "fallback"), "fallback", "a missing key answers the default");

	const advert::KvNode* block = root->Find("block");
	Check(block != nullptr && block->IsBlock(), "a nested block is a block");
	if (block != nullptr) {
		CheckEqStr(block->GetString("nested"), "inner", "a nested value");
		const advert::KvNode* deep = block->Find("block");
		Check(deep != nullptr, "a nested block inside a block");
		if (deep != nullptr) {
			CheckEqStr(deep->GetString("deep"), "42", "a value two levels down");
			CheckEqInt(deep->GetInt("deep"), 42, "the same value as an int");
		}
	}

	const advert::KvNode* both = root->Find("both");
	Check(both != nullptr && both->hasValue && both->IsBlock(), "a key may carry a value and a block");
	if (both != nullptr) {
		CheckEqStr(both->value, "value", "the value of a key that also has a block");
		CheckEqStr(both->GetString("child"), "x", "the block of a key that also has a value");
	}

	// Find() answers the first duplicate, like Valve's FindKey; the parser keeps both in order.
	CheckEqStr(root->GetString("dup"), "1", "the first duplicate key wins on lookup");
	int duplicates = 0;
	for (const advert::KvNode& child : root->children) {
		if (child.name == "dup") {
			++duplicates;
		}
	}
	CheckEqInt(duplicates, 2, "both duplicate keys are kept");
}

void TestKv1Values() {
	Section("KV1 reader: value conversion");

	const std::optional<advert::KvNode> root = advert::ParseKv1(R"(
"n"
{
	"a" "42"
	"b" " 17 "
	"c" "12abc"
	"d" "abc"
	"e" "1.5"
	"f" ""
	"neg" "-3"
}
)");
	Check(root.has_value(), "the value document parses");
	if (!root) {
		return;
	}
	CheckEqInt(root->GetInt("a"), 42, "an int value");
	CheckEqInt(root->GetInt("b"), 17, "surrounding whitespace is trimmed");
	CheckEqInt(root->GetInt("c"), 12, "a trailing suffix is ignored (atoi semantics)");
	CheckEqInt(root->GetInt("d"), 0, "a non-numeric value is 0 (atoi semantics)");
	CheckEqInt(root->GetInt("neg"), -3, "a negative int value");
	CheckEqInt(root->GetInt("f", 9), 9, "an empty value answers the default");
	CheckEqInt(root->GetInt("missing", 7), 7, "a missing key answers the default");
	CheckEqFloat(root->GetFloat("e"), 1.5f, "a float value");
	CheckEqFloat(root->GetFloat("d", 3.5f), 3.5f, "a non-numeric value answers the float default");
	CheckEqFloat(root->GetFloat("missing", 20.0f), 20.0f, "a missing key answers the float default");
	CheckEqStr(root->GetString("f"), "", "an empty value is still a value");
}

void TestKv1BomAndDirectives() {
	Section("KV1 reader: BOM and directives");

	const std::optional<advert::KvNode> bom = advert::ParseKv1("\xEF\xBB\xBF\"root\" { \"key\" \"value\" }");
	Check(bom.has_value(), "a document with a UTF-8 BOM parses");
	if (bom) {
		CheckEqStr(bom->name, "root", "the BOM does not become part of the root name");
		CheckEqStr(bom->GetString("key"), "value", "the keys after a BOM are found");
	}

	// #base / #include / #define are skipped, not resolved (the old loader did the same).
	const std::optional<advert::KvNode> base =
			advert::ParseKv1("#base \"other.ini\"\n#define X 1\n\"root\" { \"a\" \"b\" }");
	Check(base.has_value() && base->GetString("a") == "b", "directive lines are skipped");

	// A file without a top-level block is root with plain keys.
	const std::optional<advert::KvNode> flat = advert::ParseKv1("\"key\" \"value\"\n\"other\" \"2\"");
	Check(flat.has_value(), "a document without a block parses");
	if (flat) {
		CheckEqStr(flat->GetString("key"), "value", "a flat key is found");
		CheckEqInt(flat->GetInt("other"), 2, "a flat int key is found");
	}

	// A single block is unwrapped into the root.
	const std::optional<advert::KvNode> single = advert::ParseKv1("\"root\" { \"key\" \"value\" }");
	Check(single.has_value() && single->name == "root", "the single block is unwrapped");
}

void TestKv1Malformed() {
	Section("KV1 reader: malformed input");

	Check(!advert::ParseKv1("\"root\" { \"a\" \"b\"").has_value(), "a missing '}' is rejected");
	Check(!advert::ParseKv1("\"root\" { }\n}").has_value(), "a stray '}' is rejected");
	Check(!advert::ParseKv1("{ \"a\" \"b\" }").has_value(), "a block without a key is rejected");
	Check(!advert::ParseKv1("\"a\" \"unterminated").has_value(), "an unterminated string is rejected");
	Check(!advert::ParseKv1("\"root\" { \"a\" \"b/* unterminated comment").has_value(),
		  "an unterminated block comment is rejected");
	Check(!advert::ParseKv1("/* never closed").has_value(), "an unterminated comment is rejected");

	Check(advert::ParseKv1("").has_value(), "an empty file is a valid empty tree");
	Check(advert::ParseKv1("   \n\t\n").has_value(), "a whitespace-only file is a valid empty tree");
	Check(advert::ParseKv1("// only a comment\n").has_value(), "a comment-only file is valid");
}

// ==============================================================================================
// 2. the shipped advertisement.ini
// ==============================================================================================

void TestShippedConfig() {
	Section("config: the shipped advertisement.ini");

	const std::string shipped = ShippedConfigPath();
	const std::optional<advert::Config> loaded = advert::LoadConfigFromFile(shipped);
	Check(loaded.has_value(), std::string("the shipped file parses: ") + shipped);
	if (!loaded) {
		return;
	}
	const advert::Config& config = *loaded;

	CheckEqFloat(config.interval, 20.0f, "the shipped update time is 20 s");
	// Nine entries: the file repeats the key "5", and the original iterated the blocks in file
	// order, duplicates included.
	CheckEqInt(static_cast<int64_t>(config.entries.size()), 9, "nine entries in file order");
	if (config.entries.size() != 9) {
		return;
	}

	for (const advert::Entry& entry : config.entries) {
		CheckEqInt(static_cast<int64_t>(entry.messages.size()), 1, "every shipped entry has one message");
	}

	// 1: "text" with five trimmed lines, including the "ip:" lines.
	const advert::Entry& first = config.entries[0];
	Check(first.messages[0].target == advert::Target::Chat, "entry 1 is chat");
	CheckEqInt(static_cast<int64_t>(first.messages[0].lines.size()), 5, "entry 1 has five lines");
	if (first.messages[0].lines.size() == 5) {
		CheckEqStr(first.messages[0].lines[0], "{LIME} ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬", "entry 1 line 1");
		CheckEqStr(first.messages[0].lines[1],
				   "{RED}☆☆☆ {GREEN}Не теряйтесь! {OLIVE}Добавьте в избранное наши сервера:", "entry 1 line 2");
		CheckEqStr(first.messages[0].lines[2],
				   "{RED}☆☆☆ {GREEN}BHOP MIRAGE ip: {RED}bhop.example.com:27015", "entry 1 line 3");
		CheckEqStr(first.messages[0].lines[3],
				   "{RED}☆☆☆ {GREEN}ONLY DUST2 ip: {RED}d2.example.com:27015", "entry 1 line 4");
		CheckEqStr(first.messages[0].lines[4], "{LIME} ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬", "entry 1 line 5");
	}

	// Keys 2, 4-6 and 8 (entries 1, 3, 4, 5, 7): single-line centre messages.
	Check(config.entries[1].messages[0].target == advert::Target::Center, "entry 2 is centre");
	CheckEqStr(config.entries[1].messages[0].lines[0], "Авторизуйся на example.com и измени своего перса",
			   "entry 2 text");
	CheckEqStr(config.entries[3].messages[0].lines[0], "Пропиши !viptest и получи плюшки!!!", "entry 4 text");
	CheckEqStr(config.entries[4].messages[0].lines[0], "Ножи, Перчи, Агенты и прочее на сайте example.com",
			   "entry 5 text");
	CheckEqStr(config.entries[5].messages[0].lines[0],
			   "Открылись сервера в Новосибирске. С низким пингом для Сибиряков", "entry 6 text");
	CheckEqStr(config.entries[7].messages[0].lines[0],
			   "Открылись сервера в CSGO на локациях Москва и Новосибирск", "entry 8 text");

	// Keys 3 and 7 (entries 2 and 6): four- and five-line chat messages.
	Check(config.entries[2].messages[0].target == advert::Target::Chat, "entry 3 is chat");
	CheckEqInt(static_cast<int64_t>(config.entries[2].messages[0].lines.size()), 4, "entry 3 has four lines");
	CheckEqStr(config.entries[2].messages[0].lines[1], "{RED}☆☆☆ {OLIVE}Наш телеграмм: {BLUE}t.me/alkosrv",
			   "entry 3 line 2");
	CheckEqStr(config.entries[2].messages[0].lines[2],
			   "{RED}☆☆☆ {OLIVE}Наш Discord: {BLUE}discord.gg/vYgt92QQxr", "entry 3 line 3");
	CheckEqInt(static_cast<int64_t>(config.entries[6].messages[0].lines.size()), 5, "entry 7 has five lines");

	// The duplicate "5" is the last entry (file order, not key order).
	Check(config.entries[8].messages[0].target == advert::Target::Center, "entry 9 is centre");
	CheckEqStr(config.entries[8].messages[0].lines[0],
			   "Мониторинг серверов и все скины в скинченджере сайте example.com", "entry 9 text");
}

// ==============================================================================================
// 3. the message model in memory
// ==============================================================================================

void TestDestAndSinks() {
	Section("config: legacy dest and the named sinks");
	const std::optional<advert::Config> loaded = advert::LoadConfigFromText(R"(
"advertisement"
{
	"update time" "7"
	"list"
	{
		"a" { "dest" "1" "text" "console one" }
		"b" { "dest" "2" "text" "console two" }
		"c" { "dest" "3" "text" "chat three" }
		"d" { "dest" "4" "text" "hud four" }
		"e" { "dest" "5" "text" "chat five" }
		"f" { "dest" "6" "text" "center six" }
		"g" { "dest" "9" "text" "chat default" }
		"h" { "text" "plain chat" "chat" "secondary chat" }
		"i" { "center" "to the centre" "hud" "to the alert" "html" "to the panel" "console" "to the console" }
		"j" { "center" "   " }
		"k" { "text" "" }
	}
}
)");
	Check(loaded.has_value(), "the in-memory config parses");
	if (!loaded) {
		return;
	}
	const advert::Config& config = *loaded;
	CheckEqFloat(config.interval, 7.0f, "the interval is read");
	CheckEqInt(static_cast<int64_t>(config.entries.size()), 9, "\"j\" and \"k\" carry no message");
	if (config.entries.size() != 9) {
		return;
	}
	Check(config.entries[0].messages[0].target == advert::Target::Console, "dest 1 is the console");
	Check(config.entries[1].messages[0].target == advert::Target::Console, "dest 2 is the console");
	Check(config.entries[2].messages[0].target == advert::Target::Chat, "dest 3 is chat");
	Check(config.entries[3].messages[0].target == advert::Target::Hud, "dest 4 is the alert line");
	Check(config.entries[4].messages[0].target == advert::Target::Chat, "dest 5 is chat");
	Check(config.entries[5].messages[0].target == advert::Target::Center, "dest 6 is the centre");
	Check(config.entries[6].messages[0].target == advert::Target::Chat, "an unknown dest is chat");

	// "h": both "text" and "chat" become chat messages.
	CheckEqInt(static_cast<int64_t>(config.entries[7].messages.size()), 2, "\"text\" and \"chat\" both count");
	Check(config.entries[7].messages[0].target == advert::Target::Chat, "the first of the pair is chat");
	CheckEqStr(config.entries[7].messages[0].lines[0], "plain chat", "\"text\" text");
	CheckEqStr(config.entries[7].messages[1].lines[0], "secondary chat", "\"chat\" text");

	// "i": the four named sinks, in the order of the original (center, hud, html, console).
	CheckEqInt(static_cast<int64_t>(config.entries[8].messages.size()), 4, "the four named sinks");
	Check(config.entries[8].messages[0].target == advert::Target::Center, "center first");
	Check(config.entries[8].messages[1].target == advert::Target::Hud, "hud second");
	Check(config.entries[8].messages[2].target == advert::Target::Html, "html third");
	Check(config.entries[8].messages[3].target == advert::Target::Console, "console fourth");
	CheckEqStr(config.entries[8].messages[0].lines[0], "to the centre", "the centre text");
	CheckEqStr(config.entries[8].messages[3].lines[0], "to the console", "the console text");

	CheckEqInt(static_cast<int64_t>(advert::TargetFromDest(1)), static_cast<int64_t>(advert::Target::Console),
			   "TargetFromDest(1)");
	CheckEqInt(static_cast<int64_t>(advert::TargetFromDest(4)), static_cast<int64_t>(advert::Target::Hud),
			   "TargetFromDest(4)");
	CheckEqInt(static_cast<int64_t>(advert::TargetFromDest(6)), static_cast<int64_t>(advert::Target::Center),
			   "TargetFromDest(6)");
	CheckEqInt(static_cast<int64_t>(advert::TargetFromDest(0)), static_cast<int64_t>(advert::Target::Chat),
			   "TargetFromDest(0)");
}

void TestIntervalAndDefaults() {
	Section("config: interval clamp and defaults");

	const std::optional<advert::Config> zero =
			advert::LoadConfigFromText(R"("advertisement" { "update time" "0" "list" { "1" { "text" "x" } } })");
	Check(zero.has_value() && zero->interval == advert::kMinimumInterval, "an update time of 0 is clamped to 1 s");

	const std::optional<advert::Config> negative =
			advert::LoadConfigFromText(R"("advertisement" { "update time" "-5" "list" { "1" { "text" "x" } } })");
	Check(negative.has_value() && negative->interval == advert::kMinimumInterval,
		  "a negative update time is clamped");

	const std::optional<advert::Config> nan =
			advert::LoadConfigFromText(R"("advertisement" { "update time" "nan" })");
	Check(nan.has_value() && nan->interval == advert::kMinimumInterval, "a NaN update time is clamped");

	const std::optional<advert::Config> missing =
			advert::LoadConfigFromText(R"("advertisement" { "list" { "1" { "text" "x" } } })");
	Check(missing.has_value() && missing->interval == advert::kDefaultInterval,
		  "a missing update time keeps the 20 s default");

	const std::optional<advert::Config> garbled =
			advert::LoadConfigFromText(R"("advertisement" { "update time" "abc" })");
	Check(garbled.has_value() && garbled->interval == advert::kDefaultInterval,
		  "a non-numeric update time keeps the 20 s default");

	const std::optional<advert::Config> fractional =
			advert::LoadConfigFromText(R"("advertisement" { "update time" "7.5" })");
	Check(fractional.has_value() && fractional->interval == 7.5f, "a fractional update time is kept");

	const std::optional<advert::Config> noList =
			advert::LoadConfigFromText(R"("advertisement" { "update time" "9" })");
	Check(noList.has_value() && noList->entries.empty() && noList->interval == 9.0f,
		  "a config without a list loads with no entries");

	Check(!advert::LoadConfigFromText("\"advertisement\" { \"update time\" ").has_value(),
		  "a malformed config text is rejected");
}

void TestSplitLinesAndJoin() {
	Section("split/join");
	const std::vector<std::string> lines = advert::SplitLines("  one  \n\t two\n\n   \nthree ");
	CheckEqInt(static_cast<int64_t>(lines.size()), 3, "three non-empty lines");
	if (lines.size() == 3) {
		CheckEqStr(lines[0], "one", "line 1 is trimmed");
		CheckEqStr(lines[1], "two", "line 2 is trimmed");
		CheckEqStr(lines[2], "three", "line 3 is trimmed");
	}
	Check(advert::SplitLines("").empty(), "an empty text has no lines");
	Check(advert::SplitLines("   \n\t\n").empty(), "a blank text has no lines");

	// A literal backslash-n (not an escape) still splits, like the original's safety net.
	const std::vector<std::string> escaped = advert::SplitLines("a\\nb");
	CheckEqInt(static_cast<int64_t>(escaped.size()), 2, "a literal \\n splits");
	if (escaped.size() == 2) {
		CheckEqStr(escaped[0], "a", "the part before \\n");
		CheckEqStr(escaped[1], "b", "the part after \\n");
	}

	CheckEqStr(advert::Join({"a", "b", "c"}, "<br>"), "a<br>b<br>c", "Join with <br>");
	CheckEqStr(advert::Join({}, "x"), "", "Join of nothing");
	CheckEqStr(advert::Join({"only"}, "x"), "only", "Join of one line");
}

// ==============================================================================================
// 4. Colorize
// ==============================================================================================

void TestColorize() {
	Section("Colorize: the cs2-menus tag table");

	CheckEqStr(advert::Colorize("{DEFAULT}x"), "\x01" "x", "{DEFAULT}");
	CheckEqStr(advert::Colorize("{WHITE}x"), "\x01" "x", "{WHITE}");
	CheckEqStr(advert::Colorize("{RED}x"), "\x02" "x", "{RED}");
	CheckEqStr(advert::Colorize("{LIGHTPURPLE}x"), "\x03" "x", "{LIGHTPURPLE}");
	CheckEqStr(advert::Colorize("{GREEN}x"), "\x04" "x", "{GREEN}");
	CheckEqStr(advert::Colorize("{LIME}x"), "\x05" "x", "{LIME}");
	CheckEqStr(advert::Colorize("{LIGHTGREEN}x"), "\x06" "x", "{LIGHTGREEN}");
	CheckEqStr(advert::Colorize("{DARKRED}x"), "\x07" "x", "{DARKRED}");
	CheckEqStr(advert::Colorize("{GRAY}x"), "\x08" "x", "{GRAY}");
	CheckEqStr(advert::Colorize("{LIGHTOLIVE}x"), "\x09" "x", "{LIGHTOLIVE}");
	CheckEqStr(advert::Colorize("{OLIVE}x"), "\x10" "x", "{OLIVE}");
	CheckEqStr(advert::Colorize("{LIGHTBLUE}x"), "\x0B" "x", "{LIGHTBLUE}");
	CheckEqStr(advert::Colorize("{BLUE}x"), "\x0C" "x", "{BLUE}");
	CheckEqStr(advert::Colorize("{PURPLE}x"), "\x0E" "x", "{PURPLE}");
	CheckEqStr(advert::Colorize("{LIGHTRED}x"), "\x0F" "x", "{LIGHTRED}");
	CheckEqStr(advert::Colorize("{GRAYBLUE}x"), "\x0A" "x", "{GRAYBLUE}");

	// The chat line separator: the phrase files spell it "\n" (backslash + n).
	CheckEqStr(advert::Colorize("a\\nb"), "a\xE2\x80\xA9" "b", "the \\n tag is U+2029");

	CheckEqStr(advert::Colorize("{RED}a{GREEN}b"), "\x02" "a" "\x04" "b", "several tags in one line");
	CheckEqStr(advert::Colorize("no tags here"), "no tags here", "plain text is untouched");
	CheckEqStr(advert::Colorize("{UNKNOWN}"), "{UNKNOWN}", "an unknown tag is untouched");
	CheckEqStr(advert::Colorize("Привет {RED}мир"), "Привет \x02" "мир", "UTF-8 text is untouched");
	CheckEqStr(advert::Colorize(""), "", "empty text");
}

// ==============================================================================================
// 5. where the config is read from
// ==============================================================================================

void TestConfigLocations() {
	Section("config locations");

	const std::vector<std::string> candidates = advert::ConfigCandidates("/srv/configs");
	CheckEqInt(static_cast<int64_t>(candidates.size()), 2, "two candidates");
	if (candidates.size() == 2) {
		CheckEqStr(candidates[0], "/srv/configs/advertisement/advertisement.ini",
				   "the plugin folder is the primary location");
		CheckEqStr(candidates[1], "/srv/configs/advertisement.ini", "the flat file is the fallback");
	}
	const std::vector<std::string> trailing = advert::ConfigCandidates("/srv/configs/");
	Check(!trailing.empty() && trailing[0] == "/srv/configs/advertisement/advertisement.ini",
		  "a trailing slash does not double the separator");
	Check(advert::ConfigCandidates("").empty(), "without a configs directory there is no candidate");

	CheckEqStr(advert::JoinPath("", "x.ini"), "x.ini", "JoinPath with an empty base");
	CheckEqStr(advert::JoinPath("/a", ""), "/a", "JoinPath with an empty name");
	CheckEqStr(advert::JoinPath("/a/", "/b"), "/a/b", "JoinPath inserts exactly one separator");
	Check(advert::FileExists(ShippedConfigPath()), "the shipped test file exists");
	Check(!advert::FileExists("/no/such/advertisement.ini"), "a missing file does not exist");

	ResetAll();
	const std::string dir = PathString(TempRoot());

	// Nothing on disk.
	std::string used = "sentinel";
	Check(!advert::LoadConfigFromDir(dir, &used).has_value(), "no config file -> nullopt");
	Check(used.empty(), "no config file -> usedPath is empty");

	// Only the fallback file exists.
	WriteFile(TempRoot() / "advertisement.ini",
			  R"("advertisement" { "update time" "5" "list" { "1" { "center" "fallback" } } })");
	const std::optional<advert::Config> fallback = advert::LoadConfigFromDir(dir, &used);
	Check(fallback.has_value(), "the fallback file is read");
	CheckEqStr(used, advert::JoinPath(dir, "advertisement.ini"), "usedPath names the fallback");
	Check(fallback.has_value() && fallback->interval == 5.0f, "the fallback file's interval");
	Check(fallback.has_value() && fallback->entries.size() == 1 &&
				  fallback->entries[0].messages[0].lines[0] == "fallback",
		  "the fallback file's entry");

	// Both exist: the plugin folder wins.
	WriteFile(TempRoot() / "advertisement" / "advertisement.ini",
			  R"("advertisement" { "update time" "7" "list" { "1" { "center" "primary" } } })");
	const std::optional<advert::Config> primary = advert::LoadConfigFromDir(dir, &used);
	Check(primary.has_value(), "the primary file is read");
	CheckEqStr(used, advert::JoinPath(advert::JoinPath(dir, "advertisement"), "advertisement.ini"),
			   "usedPath names the primary file");
	Check(primary.has_value() && primary->interval == 7.0f, "the primary file's interval");
	Check(primary.has_value() && primary->entries[0].messages[0].lines[0] == "primary",
		  "the primary file's entry");

	// A malformed primary is not silently replaced by a stale fallback.
	WriteFile(TempRoot() / "advertisement" / "advertisement.ini", R"("advertisement" { "update time" "7")");
	Check(!advert::LoadConfigFromDir(dir, &used).has_value(),
		  "a malformed primary is rejected instead of falling back");
	CheckEqStr(used, advert::JoinPath(advert::JoinPath(dir, "advertisement"), "advertisement.ini"),
			   "usedPath still names the malformed primary");

	Check(!advert::LoadConfigFromDir("", &used).has_value(), "an empty configs directory reads nothing");
	Check(!advert::LoadConfigFromFile("/no/such/file.ini").has_value(), "a missing file is nullopt");
}

// ==============================================================================================
// 6-8. the plugin itself: map gate, interval, outputs
// ==============================================================================================

void TestMapGate() {
	Section("map gate (PITFALLS B1/B2)");

	// 1. Without the map-listener bindings the gate stays shut for good (fail soft, never Failed).
	ResetAll();
	__s2sdk_OnMapStart_Register = nullptr;
	__s2sdk_OnMapEnd_Register = nullptr;
	Check(static_cast<bool>(g_plugin.OnPluginStart()), "OnPluginStart succeeds without the map listeners");
	Check(!g_mapReady, "without the map listeners the gate stays shut");
	Check(HasErrorLog() && HasLogContaining("map listeners"), "the missing listener is logged as an error");
	Advance(5000);
	Check(!g_mapReady, "an update does not open the gate without the map listeners");
	CheckNoOutput("nothing is printed without the map listeners");
	g_plugin.OnPluginEnd();
	CheckEqInt(g_mapStartRegistrations, 0, "no listener was registered");

	// 2. A normal start registers both listeners once and prints nothing before a map.
	StartPlugin(kAbConfig);
	CheckEqInt(g_mapStartRegistrations, 1, "the OnMapStart listener is registered once");
	CheckEqInt(g_mapEndRegistrations, 1, "the OnMapEnd listener is registered once");
	Check(!g_mapReady, "the gate is shut until a map start or the first update");
	CheckNoOutput("OnPluginStart prints nothing");

	// 3. A plugin loaded into a running map: the first update proves a live map.
	Advance(16);
	Check(g_mapReady, "the first update after OnPluginStart opens the gate");
	CheckNoOutput("the first update shows nothing before the interval elapsed");

	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "the first ad is shown after the interval");
	CheckEqStr(g_chatAll.empty() ? "" : g_chatAll[0], " alpha", "the first ad is the first entry");
	ResetOutputs();

	// 4. OnMapEnd closes the gate; nothing is printed between the maps.
	FireMapEnd();
	Check(!g_mapReady, "OnMapEnd closes the gate");
	Check(g_fireOnReady, "the map change parks the message that was due");
	CheckEqFloat(g_elapsed, 0.0f, "OnMapEnd drops the elapsed time");
	Advance(5000);
	Check(!g_mapReady, "the host keeps pumping updates through a changelevel: the gate stays shut");
	CheckNoOutput("nothing is printed between OnMapEnd and the next OnMapStart");

	// 5. OnMapStart re-opens the gate and re-reads the config; the parked message is shown on the
	//    first ready update (the old repeating timer was re-armed one tick after the map start).
	g_logLines.clear();
	g_logSeverities.clear();
	FireMapStart();
	Check(g_mapReady, "OnMapStart re-opens the gate");
	Check(HasLogContaining("loaded 2 entries"), "a map start reloads the config and logs it");
	CheckNoOutput("the map-start listener itself prints nothing");
	Advance(16);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1,
			   "the parked message is shown on the first ready update");
	CheckEqStr(g_chatAll.empty() ? "" : g_chatAll[0], " beta", "the rotation continued across the map change");
	ResetOutputs();

	// 6. The config is re-read on every map start (MapStartHook of the original).
	WriteAppConfig(R"("advertisement" { "update time" "1" "list" { "1" { "text" "reloaded" } } })");
	FireMapEnd();
	Check(!g_mapReady, "the second map end closes the gate again");
	FireMapStart();
	CheckEqInt(static_cast<int64_t>(g_entries.size()), 1, "OnMapStart re-reads the config");
	Advance(16);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "one message from the re-read config");
	CheckEqStr(g_chatAll.empty() ? "" : g_chatAll[0], " reloaded", "the message comes from the new file");
	ResetOutputs();

	// 7. Clean unload (PITFALLS A7): both listeners are removed and the state is dropped.
	g_plugin.OnPluginEnd();
	CheckEqInt(g_mapStartUnregistrations, 1, "the OnMapStart listener is unregistered once");
	CheckEqInt(g_mapEndUnregistrations, 1, "the OnMapEnd listener is unregistered once");
	Check(!g_mapListenersRegistered, "the map listeners are no longer registered");
	Check(!g_mapReady, "the gate is closed after OnPluginEnd");
	Check(g_entries.empty(), "the loaded entries are dropped on unload");
	Advance(5000);
	CheckNoOutput("a stale update after OnPluginEnd prints nothing");

	// 8. A reload registers everything exactly once again.
	Check(static_cast<bool>(g_plugin.OnPluginStart()), "a reload starts again");
	CheckEqInt(g_mapStartRegistrations, 2, "the OnMapStart listener is registered a second time");
	CheckEqInt(g_mapEndRegistrations, 2, "the OnMapEnd listener is registered a second time");
	Advance(16);
	Check(g_mapReady, "the gate opens again after a reload");
	g_plugin.OnPluginEnd();
}

void TestOutputs() {
	Section("the five output sinks");

	StartPlugin(kSinksConfig);
	Check(HasLogContaining("loaded 5 entries"), "the five entries are logged");
	Advance(16); // gate opens

	// 1. "text": each line to chat, colour-tagged, with the leading space of the original.
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 2, "the two text lines reach the chat");
	if (g_chatAll.size() == 2) {
		CheckEqStr(g_chatAll[0], " first line", "chat line 1 keeps the leading space");
		CheckEqStr(g_chatAll[1], " second line", "chat line 2 keeps the leading space");
	}

	// 2. "center": the lines joined with a real newline.
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_center.size()), 1, "the centre message");
	CheckEqStr(g_center.empty() ? "" : g_center[0], "centered text", "the centre text");

	// 3. "hud": the alert line.
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_alert.size()), 1, "the alert message");
	CheckEqStr(g_alert.empty() ? "" : g_alert[0], "alert text", "the alert text");

	// 4. "html": the lines joined with <br>, shown for kHtmlSeconds.
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_html.size()), 1, "the centre-HTML message");
	CheckEqStr(g_html.empty() ? "" : g_html[0], "html one<br>html two", "the HTML text joins with <br>");
	CheckEqInt(g_htmlDurations.empty() ? -1 : g_htmlDurations[0], advert::kHtmlSeconds,
			   "the HTML duration is kHtmlSeconds");

	// 5. "console": every line with its trailing newline.
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_console.size()), 2, "the console message has two lines");
	if (g_console.size() == 2) {
		CheckEqStr(g_console[0], "console one\n", "console line 1 ends with a newline");
		CheckEqStr(g_console[1], "console two\n", "console line 2 ends with a newline");
	}

	// The rotation wraps around to the first entry.
	ResetOutputs();
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 2, "the rotation wraps around to the first entry");
	g_plugin.OnPluginEnd();
}

void TestChatColorizeAndFallback() {
	Section("chat colour tags and the per-slot fallback");

	StartPlugin(R"("advertisement" { "update time" "1" "list" { "1" { "text" "{RED}red {LIME}lime" } } })");
	Advance(16);

	// PrintToChatAll is the direct path: the tags become control bytes, the line keeps its space.
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "one colour-tagged chat line");
	CheckEqStr(g_chatAll.empty() ? "" : g_chatAll[0], " \x02" "red " "\x05" "lime",
			   "the chat colours are the cs2-menus bytes");

	// Without PrintToChatAll the same line goes per slot through PrintToChat.
	ResetOutputs();
	__s2sdk_PrintToChatAll = nullptr;
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_chatSlotTexts.size()), 64, "the fallback prints once per slot");
	CheckEqInt(static_cast<int64_t>(g_chatSlotSlots.size()), 64, "one recorded slot per print");
	bool slotsInOrder = !g_chatSlotSlots.empty();
	for (std::size_t i = 0; i < g_chatSlotSlots.size(); ++i) {
		if (g_chatSlotSlots[i] != static_cast<int32_t>(i)) {
			slotsInOrder = false;
		}
	}
	Check(slotsInOrder, "the fallback walks slots 0..63 in order");
	bool allColorized = !g_chatSlotTexts.empty();
	for (const std::string& text : g_chatSlotTexts) {
		if (text != " \x02" "red " "\x05" "lime") {
			allColorized = false;
		}
	}
	Check(allColorized, "every slot gets the colour-tagged line");

	// With neither binding the plugin stays silent instead of crashing.
	ResetOutputs();
	__s2sdk_PrintToChat = nullptr;
	Advance(1000);
	CheckNoOutput("no chat binding -> no output and no crash");
	g_plugin.OnPluginEnd();
}

void TestIntervalFloor() {
	Section("update-driven interval (PITFALLS C9)");

	StartPlugin(kAbConfig);
	Advance(16); // gate opens
	Check(g_mapReady, "the gate is open");

	// A zero interval (impossible from the clamped config, but the plugin must not loop) is
	// driven at one server tick: 10 ms + 10 ms crosses 1/64 s, one update alone does not.
	g_interval = 0.0f;
	g_elapsed = 0.0f;
	ResetOutputs();
	Advance(10);
	CheckNoOutput("10 ms is below the 1/64 s floor");
	Advance(10);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "20 ms crosses the 1/64 s floor exactly once");

	// A negative interval is floored the same way.
	g_interval = -5.0f;
	g_elapsed = 0.0f;
	ResetOutputs();
	Advance(10);
	CheckNoOutput("a negative interval is still floored at 1/64 s");
	Advance(10);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "a negative interval fires after one tick");

	// NaN is treated as "one tick", never as a zero delay.
	g_interval = std::numeric_limits<float>::quiet_NaN();
	g_elapsed = 0.0f;
	ResetOutputs();
	Advance(10);
	CheckNoOutput("a NaN interval is floored at 1/64 s");
	Advance(10);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "a NaN interval fires after one tick");

	// A huge dt shows at most one entry: the old repeating timer fired once per frame at most and
	// re-armed from the moment the callback returned, it never burned through the rotation.
	g_interval = 1.0f;
	g_elapsed = 0.0f;
	g_next = 0;
	ResetOutputs();
	Advance(5000);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "one huge update shows exactly one entry");

	// The rotation still advances by one per fire.
	ResetOutputs();
	Advance(1000);
	CheckEqInt(static_cast<int64_t>(g_chatAll.size()), 1, "the next interval shows one entry");
	CheckEqStr(g_chatAll.empty() ? "" : g_chatAll[0], " beta", "the rotation advanced by exactly one");
	g_plugin.OnPluginEnd();
}

// ==============================================================================================
// 9. fail-soft paths
// ==============================================================================================

void TestFailSoft() {
	Section("fail soft (PITFALLS A1)");

	// No config file: the plugin logs the failure, stays loaded and prints nothing.
	ResetAll();
	g_configsDir = PathString(TempRoot()); // the directory exists but is empty
	Check(static_cast<bool>(g_plugin.OnPluginStart()), "OnPluginStart succeeds without a config file");
	Check(HasErrorLog() && HasLogContaining("Failed to load"), "the missing config is logged as an error");
	Check(g_entries.empty(), "no entries without a config file");
	Advance(16);
	Advance(20000);
	CheckNoOutput("no config -> no advertisement");
	g_plugin.OnPluginEnd();

	// No Plugify configs directory at all: one error line, no crash.
	ResetAll();
	plg::GetConfigsDir = nullptr;
	Check(static_cast<bool>(g_plugin.OnPluginStart()), "OnPluginStart succeeds without the configs dir");
	Check(HasLogContaining("configs directory is unavailable"), "the missing configs dir is logged");
	Advance(16);
	Advance(20000);
	CheckNoOutput("no configs dir -> no advertisement");
	g_plugin.OnPluginEnd();

	// A malformed config: the whole load is rejected, not half-read.
	ResetAll();
	WriteAppConfig(R"("advertisement" { "update time" "1" "list" { "1" { "text" "broken" )");
	Check(static_cast<bool>(g_plugin.OnPluginStart()), "OnPluginStart succeeds with a malformed config");
	Check(HasErrorLog(), "the malformed config is logged as an error");
	Check(g_entries.empty(), "a malformed config contributes no entries");
	g_plugin.OnPluginEnd();

	// Every output binding missing: the plugin still runs and prints nothing.
	ResetAll();
	WriteAppConfig(kAbConfig);
	__s2sdk_PrintToChatAll = nullptr;
	__s2sdk_PrintToChat = nullptr;
	__s2sdk_PrintCenterTextAll = nullptr;
	__s2sdk_PrintAlertTextAll = nullptr;
	__s2sdk_PrintCentreHtmlAll = nullptr;
	__s2sdk_PrintToConsoleAll = nullptr;
	Check(static_cast<bool>(g_plugin.OnPluginStart()), "OnPluginStart succeeds without the print bindings");
	Advance(16);
	Advance(2000);
	CheckNoOutput("without the print bindings nothing is printed");
	g_plugin.OnPluginEnd();
}

} // namespace

int main() {
	TestKv1Reader();
	TestKv1Values();
	TestKv1BomAndDirectives();
	TestKv1Malformed();
	TestShippedConfig();
	TestDestAndSinks();
	TestIntervalAndDefaults();
	TestSplitLinesAndJoin();
	TestColorize();
	TestConfigLocations();
	TestMapGate();
	TestOutputs();
	TestChatColorizeAndFallback();
	TestIntervalFloor();
	TestFailSoft();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
