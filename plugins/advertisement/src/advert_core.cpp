#include "advert_core.hpp"

#include <cstddef>
#include <utility>

namespace advert {
namespace {

void AddMessage(Entry& entry, Target target, const char* text) {
	std::vector<std::string> lines = SplitLines(text);
	if (!lines.empty()) {
		entry.messages.push_back({target, std::move(lines)});
	}
}

} // namespace

std::vector<std::string> SplitLines(const char* text) {
	std::string value(text ? text : "");
	// The config may write line breaks as a literal backslash-n (the KeyValues parser already
	// turns them into newlines, so this only catches a file that contains "\\n").
	for (size_t pos = 0; (pos = value.find("\\n", pos)) != std::string::npos;) {
		value.replace(pos, 2, "\n");
		pos += 1;
	}
	std::vector<std::string> lines;
	size_t start = 0;
	while (start <= value.size()) {
		size_t end = value.find('\n', start);
		if (end == std::string::npos) {
			end = value.size();
		}
		const std::string line = value.substr(start, end - start);
		const size_t first = line.find_first_not_of(" \t\r");
		if (first != std::string::npos) {
			const size_t last = line.find_last_not_of(" \t\r");
			lines.push_back(line.substr(first, last - first + 1));
		}
		start = end + 1;
	}
	return lines;
}

std::string Join(const std::vector<std::string>& lines, const char* separator) {
	std::string out;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i) {
			out += separator;
		}
		out += lines[i];
	}
	return out;
}

Target TargetFromDest(int dest) {
	switch (dest) {
		case 1:
		case 2:
			return Target::Console;
		case 4:
			return Target::Hud;
		case 6:
			return Target::Center;
		default:
			return Target::Chat;
	}
}

Config ParseConfig(pisex::KeyValues& root, float defaultInterval) {
	Config config;
	config.interval = root.GetFloat("update time", defaultInterval);
	if (!(config.interval >= kMinimumInterval)) {
		config.interval = kMinimumInterval;
	}
	pisex::KeyValues* list = root.FindKey("list");
	if (!list) {
		return config;
	}
	// Every entry in file order, duplicate keys included.
	for (pisex::KeyValues* item = list->GetFirstTrueSubKey(); item; item = item->GetNextTrueSubKey()) {
		Entry entry;
		const int dest = item->GetInt("dest", 0);
		if (dest > 0) {
			AddMessage(entry, TargetFromDest(dest), item->GetString("text", ""));
		} else {
			AddMessage(entry, Target::Chat, item->GetString("text", ""));
			AddMessage(entry, Target::Chat, item->GetString("chat", ""));
		}
		AddMessage(entry, Target::Center, item->GetString("center", ""));
		AddMessage(entry, Target::Hud, item->GetString("hud", ""));
		AddMessage(entry, Target::Html, item->GetString("html", ""));
		AddMessage(entry, Target::Console, item->GetString("console", ""));
		if (!entry.messages.empty()) {
			config.entries.push_back(std::move(entry));
		}
	}
	return config;
}

} // namespace advert
