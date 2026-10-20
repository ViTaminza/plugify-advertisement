// Tests for plugins/advertisement/src/advert_core.hpp (engine-independent core of the ported
// "advertisement" plugin).
//
//   * the real shipped file (advertisement.ini) parsed exactly
//     like the plugin does it: every "list" entry in file order, duplicate keys included,
//     "text"/"center" messages and the trimmed lines of the multi-line texts,
//   * the in-memory shapes: the legacy numeric "dest", "chat"/"hud"/"html"/"console", empty
//     values, the interval clamp and the defaults of a missing/partial file.

#include <plg/plugin.hpp>

#include <pisex/keyvalues.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "advert_core.hpp"

// A real plugin gets these from PLUGIFY_PLUGIN; the test leaves them null (also the state the
// code has to survive: no plugify dirs, no logger).
namespace plg {
GetBaseDirFn GetBaseDir{nullptr};
GetExtensionsDirFn GetExtensionsDir{nullptr};
GetConfigsDirFn GetConfigsDir{nullptr};
GetDataDirFn GetDataDir{nullptr};
GetLogsDirFn GetLogsDir{nullptr};
GetCacheDirFn GetCacheDir{nullptr};
IsLoadedFn IsLoaded{nullptr};
LogFn Log{nullptr};
BeginZoneFn BeginZone{nullptr};
EndZoneFn EndZone{nullptr};
} // namespace plg

namespace {

int g_checks = 0;
int g_failures = 0;
std::string_view g_section = "?";

void Section(std::string_view name) {
	g_section = name;
	std::printf("-- %.*s\n", static_cast<int>(name.size()), name.data());
}

#define CHECK(expr)                                                                            \
	do {                                                                                       \
		++g_checks;                                                                            \
		if (!(expr)) {                                                                         \
			++g_failures;                                                                      \
			std::printf("FAIL [%.*s] %s:%d: %s\n", static_cast<int>(g_section.size()),         \
						g_section.data(), __FILE__, __LINE__, #expr);                          \
		}                                                                                      \
	} while (false)

#define CHECK_EQ_STR(actual, expected)                                                         \
	do {                                                                                       \
		++g_checks;                                                                            \
		const std::string a_ = (actual);                                                       \
		const std::string e_ = (expected);                                                     \
		if (a_ != e_) {                                                                        \
			++g_failures;                                                                      \
			std::printf("FAIL [%.*s] %s:%d: got \"%s\", expected \"%s\"\n",                    \
						static_cast<int>(g_section.size()), g_section.data(), __FILE__,       \
						__LINE__, a_.c_str(), e_.c_str());                                     \
		}                                                                                      \
	} while (false)

#define CHECK_EQ_INT(actual, expected)                                                         \
	do {                                                                                       \
		++g_checks;                                                                            \
		const long long a_ = static_cast<long long>(actual);                                   \
		const long long e_ = static_cast<long long>(expected);                                 \
		if (a_ != e_) {                                                                        \
			++g_failures;                                                                      \
			std::printf("FAIL [%.*s] %s:%d: got %lld, expected %lld\n",                        \
						static_cast<int>(g_section.size()), g_section.data(), __FILE__,       \
						__LINE__, a_, e_);                                                     \
		}                                                                                      \
	} while (false)

advert::Config LoadBuffer(const char* text) {
	pisex::KeyValues::AutoDelete kv("advertisement");
	if (!kv->LoadFromBuffer(text)) {
		std::printf("FAIL [%.*s]: LoadFromBuffer returned false\n", static_cast<int>(g_section.size()),
					g_section.data());
	}
	return advert::ParseConfig(*kv);
}

// --- the shipped file --------------------------------------------------------------------

void TestRealConfig() {
	Section("config: production advertisement.ini");

	pisex::KeyValues::AutoDelete kv("advertisement");
	CHECK(kv->LoadFromFile(ADVERTISEMENT_TEST_CONFIG));
	const advert::Config config = advert::ParseConfig(*kv);

	CHECK_EQ_INT(config.interval, 20);
	// Nine entries: the file repeats the key "5", and the original iterated the blocks in file
	// order, duplicates included.
	CHECK_EQ_INT(config.entries.size(), 9);
	if (config.entries.size() != 9) {
		return;
	}

	for (const advert::Entry& entry : config.entries) {
		CHECK_EQ_INT(entry.messages.size(), 1);
	}

	// 1: "text" with five trimmed lines, including the "ip:" lines.
	const advert::Entry& first = config.entries[0];
	CHECK(first.messages[0].target == advert::Target::Chat);
	CHECK_EQ_INT(first.messages[0].lines.size(), 5);
	CHECK_EQ_STR(first.messages[0].lines[0],
				 "{LIME} ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬");
	CHECK_EQ_STR(first.messages[0].lines[1],
				 "{RED}☆☆☆ {GREEN}Не теряйтесь! {OLIVE}Добавьте в избранное наши сервера:");
	CHECK_EQ_STR(first.messages[0].lines[2], "{RED}☆☆☆ {GREEN}BHOP MIRAGE ip: {RED}bhop.example.com:27015");
	CHECK_EQ_STR(first.messages[0].lines[3], "{RED}☆☆☆ {GREEN}ONLY DUST2 ip: {RED}d2.example.com:27015");
	CHECK_EQ_STR(first.messages[0].lines[4],
				 "{LIME} ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬");

	// Keys 2 and 4-6, 8 (entries 1, 3, 4, 5, 7): single-line centre messages.
	CHECK(config.entries[1].messages[0].target == advert::Target::Center);
	CHECK_EQ_STR(config.entries[1].messages[0].lines[0], "Авторизуйся на example.com и измени своего перса");
	CHECK_EQ_STR(config.entries[3].messages[0].lines[0], "Пропиши !viptest и получи плюшки!!!");
	CHECK_EQ_STR(config.entries[4].messages[0].lines[0],
				 "Ножи, Перчи, Агенты и прочее на сайте example.com");
	CHECK_EQ_STR(config.entries[5].messages[0].lines[0],
				 "Открылись сервера в Новосибирске. С низким пингом для Сибиряков");
	CHECK_EQ_STR(config.entries[7].messages[0].lines[0],
				 "Открылись сервера в CSGO на локациях Москва и Новосибирск");

	// Keys 3 and 7 (entries 2 and 6): four- and five-line chat messages.
	CHECK(config.entries[2].messages[0].target == advert::Target::Chat);
	CHECK_EQ_INT(config.entries[2].messages[0].lines.size(), 4);
	CHECK_EQ_STR(config.entries[2].messages[0].lines[1],
				 "{RED}☆☆☆ {OLIVE}Наш телеграмм: {BLUE}t.me/alkosrv");
	CHECK_EQ_STR(config.entries[2].messages[0].lines[2],
				 "{RED}☆☆☆ {OLIVE}Наш Discord: {BLUE}discord.gg/vYgt92QQxr");
	CHECK_EQ_INT(config.entries[6].messages[0].lines.size(), 5);

	// The duplicate "5" is the last entry (file order, not key order).
	CHECK(config.entries[8].messages[0].target == advert::Target::Center);
	CHECK_EQ_STR(config.entries[8].messages[0].lines[0],
				 "Мониторинг серверов и все скины в скинченджере сайте example.com");
}

// --- in-memory shapes ---------------------------------------------------------------------

void TestDestAndSinks() {
	Section("config: legacy dest and the named sinks");
	const advert::Config config = LoadBuffer(R"(
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

	CHECK_EQ_INT(config.interval, 7);
	CHECK_EQ_INT(config.entries.size(), 9); // "j" and "k" carry no message
	if (config.entries.size() != 9) {
		return;
	}
	CHECK(config.entries[0].messages[0].target == advert::Target::Console);
	CHECK(config.entries[1].messages[0].target == advert::Target::Console);
	CHECK(config.entries[2].messages[0].target == advert::Target::Chat);
	CHECK(config.entries[3].messages[0].target == advert::Target::Hud);
	CHECK(config.entries[4].messages[0].target == advert::Target::Chat);
	CHECK(config.entries[5].messages[0].target == advert::Target::Center);
	CHECK(config.entries[6].messages[0].target == advert::Target::Chat);

	// "h": both "text" and "chat" become chat messages.
	CHECK_EQ_INT(config.entries[7].messages.size(), 2);
	CHECK(config.entries[7].messages[0].target == advert::Target::Chat);
	CHECK_EQ_STR(config.entries[7].messages[0].lines[0], "plain chat");
	CHECK_EQ_STR(config.entries[7].messages[1].lines[0], "secondary chat");

	// "i": the four named sinks, in the order of the original (center, hud, html, console).
	CHECK_EQ_INT(config.entries[8].messages.size(), 4);
	CHECK(config.entries[8].messages[0].target == advert::Target::Center);
	CHECK(config.entries[8].messages[1].target == advert::Target::Hud);
	CHECK(config.entries[8].messages[2].target == advert::Target::Html);
	CHECK(config.entries[8].messages[3].target == advert::Target::Console);
	CHECK_EQ_STR(config.entries[8].messages[0].lines[0], "to the centre");
	CHECK_EQ_STR(config.entries[8].messages[3].lines[0], "to the console");
}

void TestIntervalAndDefaults() {
	Section("config: interval clamp and defaults");

	const advert::Config zero = LoadBuffer(R"("advertisement" { "update time" "0" "list" { "1" { "text" "x" } } })");
	CHECK_EQ_INT(zero.interval, 1); // clamped to the minimum

	const advert::Config negative =
		LoadBuffer(R"("advertisement" { "update time" "-5" "list" { "1" { "text" "x" } } })");
	CHECK_EQ_INT(negative.interval, 1);

	const advert::Config missing = LoadBuffer(R"("advertisement" { "list" { "1" { "text" "x" } } })");
	CHECK_EQ_INT(missing.interval, 20); // kDefaultInterval

	const advert::Config noList = LoadBuffer(R"("advertisement" { "update time" "9" })");
	CHECK_EQ_INT(noList.interval, 9);
	CHECK(noList.entries.empty());
}

void TestSplitLines() {
	Section("split/join");
	const std::vector<std::string> lines = advert::SplitLines("  one  \n\t two\n\n   \nthree ");
	CHECK_EQ_INT(lines.size(), 3);
	CHECK_EQ_STR(lines[0], "one");
	CHECK_EQ_STR(lines[1], "two");
	CHECK_EQ_STR(lines[2], "three");
	CHECK(advert::SplitLines(nullptr).empty());
	CHECK(advert::SplitLines("").empty());
	CHECK(advert::SplitLines("   \n\t\n").empty());

	// A literal backslash-n (not an escape) still splits, like the original's safety net.
	const std::vector<std::string> escaped = advert::SplitLines("a\\nb");
	CHECK_EQ_INT(escaped.size(), 2);
	CHECK_EQ_STR(escaped[0], "a");
	CHECK_EQ_STR(escaped[1], "b");

	CHECK_EQ_STR(advert::Join({"a", "b", "c"}, "<br>"), "a<br>b<br>c");
	CHECK_EQ_STR(advert::Join({}, "x"), "");
	CHECK_EQ_STR(advert::Join({"only"}, "x"), "only");
}

} // namespace

int main() {
	TestRealConfig();
	TestDestAndSinks();
	TestIntervalAndDefaults();
	TestSplitLines();

	std::printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
