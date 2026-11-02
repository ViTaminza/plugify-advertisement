#include "advert_core.hpp"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

namespace advert {
namespace {

char Lower(char c) {
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool EqualNoCase(std::string_view a, std::string_view b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i) {
		if (Lower(a[i]) != Lower(b[i])) {
			return false;
		}
	}
	return true;
}

std::string_view Trim(std::string_view text) {
	std::size_t begin = 0;
	std::size_t end = text.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
		++begin;
	}
	while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}
	return text.substr(begin, end - begin);
}

// A UTF-8 byte order mark is common in files edited on Windows and would otherwise become part of
// the first key's name ("\xEF\xBB\xBFadvertisement"), which then hides the whole config.
bool StartsWithBom(std::string_view text) {
	return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
		   static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
}

// The tokenizer and grammar of the KV1 files (the subset the Pisex configs and the shipped
// advertisement.ini use), building a KvNode tree instead of pisexcompat's KeyValues:
//
//   file  := node* EOF
//   node  := token [ token ] [ '{' node* '}' ]
//   token := '"' escaped '"' | raw
//
// Every malformed shape sets failed_, which makes Parse() answer false and ParseKv1() return
// std::nullopt: the callers then keep their defaults instead of a half-read tree.
class Parser {
public:
	explicit Parser(std::string_view text) : text_(text) {}

	bool Parse(KvNode& root) {
		ParseNodes(root, false);
		return !failed_;
	}

private:
	// Skips whitespace, // line comments and /* */ block comments. A block comment that reaches
	// the end of the input is malformed (set failed_ and stop).
	void SkipSpace() {
		while (pos_ < text_.size()) {
			const char c = text_[pos_];
			if (std::isspace(static_cast<unsigned char>(c))) {
				++pos_;
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
				pos_ += 2;
				while (pos_ < text_.size() && text_[pos_] != '\n') {
					++pos_;
				}
				continue;
			}
			if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
				pos_ += 2;
				bool closed = false;
				while (pos_ + 1 < text_.size()) {
					if (text_[pos_] == '*' && text_[pos_ + 1] == '/') {
						pos_ += 2;
						closed = true;
						break;
					}
					++pos_;
				}
				if (!closed) {
					failed_ = true;
					pos_ = text_.size();
					return;
				}
				continue;
			}
			break;
		}
	}

	// One token; false at the end of the input or at a '}' (both handled by ParseNodes).
	bool ReadToken(std::string& out) {
		SkipSpace();
		if (pos_ >= text_.size() || text_[pos_] == '}') {
			return false;
		}
		out.clear();
		if (text_[pos_] == '"') {
			++pos_;
			while (pos_ < text_.size()) {
				const char c = text_[pos_++];
				if (c == '\\' && pos_ < text_.size()) {
					const char esc = text_[pos_++];
					switch (esc) {
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					default:
						// An unknown escape keeps its backslash, like Valve's loader.
						out.push_back('\\');
						out.push_back(esc);
						break;
					}
					continue;
				}
				if (c == '"') {
					return true;
				}
				out.push_back(c);
			}
			failed_ = true; // unterminated string
			return true;
		}
		while (pos_ < text_.size()) {
			const char c = text_[pos_];
			if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '"') {
				break;
			}
			if (c == '/' && pos_ + 1 < text_.size() &&
				(text_[pos_ + 1] == '/' || text_[pos_ + 1] == '*')) {
				break;
			}
			out.push_back(c);
			++pos_;
		}
		return true;
	}

	bool PeekIs(char c) {
		SkipSpace();
		return pos_ < text_.size() && text_[pos_] == c;
	}

	void ParseNodes(KvNode& parent, bool insideBlock) {
		while (true) {
			SkipSpace();
			if (failed_) {
				return;
			}
			if (pos_ >= text_.size()) {
				if (insideBlock) {
					failed_ = true; // missing '}'
				}
				return;
			}
			if (text_[pos_] == '}') {
				if (!insideBlock) {
					failed_ = true; // stray '}'
				}
				++pos_;
				return;
			}
			if (text_[pos_] == '{') {
				failed_ = true; // a block without a key
				return;
			}
			std::string key;
			if (!ReadToken(key)) {
				return;
			}
			if (!key.empty() && key[0] == '#') {
				// #base / #include / #define: no inclusion, skip the line.
				while (pos_ < text_.size() && text_[pos_] != '\n') {
					++pos_;
				}
				continue;
			}
			parent.children.push_back(KvNode{});
			// Filled through an index: the recursion below may reallocate parent.children.
			const std::size_t index = parent.children.size() - 1;
			parent.children[index].name = std::move(key);
			if (!PeekIs('{')) {
				std::string value;
				if (ReadToken(value)) {
					parent.children[index].value = std::move(value);
					parent.children[index].hasValue = true;
				}
			}
			if (PeekIs('{')) {
				++pos_;
				KvNode child = std::move(parent.children[index]);
				ParseNodes(child, true);
				parent.children[index] = std::move(child);
			}
			if (failed_) {
				return;
			}
		}
	}

	std::string_view text_;
	std::size_t pos_ = 0;
	bool failed_ = false;
};

void AddMessage(Entry& entry, Target target, std::string_view text) {
	std::vector<std::string> lines = SplitLines(text);
	if (!lines.empty()) {
		entry.messages.push_back(Message{target, std::move(lines)});
	}
}

} // namespace

// --- KvNode -------------------------------------------------------------------------------------

const KvNode* KvNode::Find(std::string_view key) const {
	if (key.empty()) {
		return nullptr;
	}
	for (const KvNode& child : children) {
		if (EqualNoCase(child.name, key)) {
			return &child;
		}
	}
	return nullptr;
}

std::string_view KvNode::GetString(std::string_view key, std::string_view def) const {
	const KvNode* found = Find(key);
	if (found == nullptr || !found->hasValue) {
		return def;
	}
	return found->value;
}

int KvNode::GetInt(std::string_view key, int def) const {
	const std::string_view text = Trim(GetString(key));
	if (text.empty()) {
		return def;
	}
	int value = 0;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
	if (result.ec != std::errc{}) {
		// Valve falls back to atoi semantics ("5 // comment", "1.5", "abc" -> 0/partial).
		return std::atoi(std::string(text).c_str());
	}
	return value;
}

float KvNode::GetFloat(std::string_view key, float def) const {
	const std::string_view text = Trim(GetString(key));
	if (text.empty()) {
		return def;
	}
	const std::string copy(text);
	char* end = nullptr;
	const float value = std::strtof(copy.c_str(), &end);
	if (end == copy.c_str()) {
		return def;
	}
	return value;
}

// --- the reader ---------------------------------------------------------------------------------

std::optional<KvNode> ParseKv1(std::string_view text) {
	if (StartsWithBom(text)) {
		text.remove_prefix(3);
	}
	KvNode root;
	Parser parser(text);
	if (!parser.Parse(root)) {
		return std::nullopt;
	}
	// A single top-level block is the file's root ("advertisement" { ... }), as in Valve's loader.
	if (root.children.size() == 1 && root.children.front().IsBlock()) {
		return std::move(root.children.front());
	}
	return root;
}

std::optional<KvNode> LoadKv1File(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return std::nullopt;
	}
	std::ostringstream content;
	content << file.rdbuf();
	const std::string text = content.str();
	return ParseKv1(text);
}

// --- the message model --------------------------------------------------------------------------

std::vector<std::string> SplitLines(std::string_view text) {
	std::string value(text);
	// The config may write line breaks as a literal backslash-n (the KV1 reader already turns them
	// into newlines, so this only catches a file that contains "\\n").
	for (std::size_t pos = 0; (pos = value.find("\\n", pos)) != std::string::npos;) {
		value.replace(pos, 2, "\n");
		pos += 1;
	}
	std::vector<std::string> lines;
	std::size_t start = 0;
	while (start <= value.size()) {
		std::size_t end = value.find('\n', start);
		if (end == std::string::npos) {
			end = value.size();
		}
		const std::string line = value.substr(start, end - start);
		const std::size_t first = line.find_first_not_of(" \t\r");
		if (first != std::string::npos) {
			const std::size_t last = line.find_last_not_of(" \t\r");
			lines.push_back(line.substr(first, last - first + 1));
		}
		start = end + 1;
	}
	return lines;
}

std::string Join(const std::vector<std::string>& lines, std::string_view separator) {
	std::string out;
	for (std::size_t i = 0; i < lines.size(); ++i) {
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

Config ParseConfig(const KvNode& root, float defaultInterval) {
	Config config;
	config.interval = root.GetFloat("update time", defaultInterval);
	if (!(config.interval >= kMinimumInterval)) {
		config.interval = kMinimumInterval;
	}
	const KvNode* list = root.Find("list");
	if (list == nullptr) {
		return config;
	}
	// Every entry in file order, duplicate keys included. Like GetFirstTrueSubKey(), only children
	// that carry a block are entries.
	for (const KvNode& item : list->children) {
		if (!item.IsBlock()) {
			continue;
		}
		Entry entry;
		const int dest = item.GetInt("dest", 0);
		if (dest > 0) {
			AddMessage(entry, TargetFromDest(dest), item.GetString("text", ""));
		} else {
			AddMessage(entry, Target::Chat, item.GetString("text", ""));
			AddMessage(entry, Target::Chat, item.GetString("chat", ""));
		}
		AddMessage(entry, Target::Center, item.GetString("center", ""));
		AddMessage(entry, Target::Hud, item.GetString("hud", ""));
		AddMessage(entry, Target::Html, item.GetString("html", ""));
		AddMessage(entry, Target::Console, item.GetString("console", ""));
		if (!entry.messages.empty()) {
			config.entries.push_back(std::move(entry));
		}
	}
	return config;
}

// --- the locations ------------------------------------------------------------------------------

std::string JoinPath(std::string_view base, std::string_view name) {
	if (base.empty()) {
		return std::string(name);
	}
	if (name.empty()) {
		return std::string(base);
	}
	std::string out(base);
	if (out.back() != '/') {
		out.push_back('/');
	}
	if (name.front() == '/') {
		name.remove_prefix(1);
	}
	out.append(name);
	return out;
}

bool FileExists(const std::string& path) {
	if (path.empty()) {
		return false;
	}
	std::ifstream file(path, std::ios::binary);
	return file.good();
}

std::vector<std::string> ConfigCandidates(std::string_view configsDir) {
	std::vector<std::string> candidates;
	if (configsDir.empty()) {
		return candidates;
	}
	candidates.reserve(2);
	candidates.push_back(JoinPath(JoinPath(configsDir, kPluginDir), kConfigFile));
	candidates.push_back(JoinPath(configsDir, kConfigFile));
	return candidates;
}

std::optional<Config> LoadConfigFromText(std::string_view text) {
	const std::optional<KvNode> root = ParseKv1(text);
	if (!root) {
		return std::nullopt;
	}
	return ParseConfig(*root);
}

std::optional<Config> LoadConfigFromFile(const std::string& path) {
	const std::optional<KvNode> root = LoadKv1File(path);
	if (!root) {
		return std::nullopt;
	}
	return ParseConfig(*root);
}

std::optional<Config> LoadConfigFromDir(std::string_view configsDir, std::string* usedPath) {
	if (usedPath != nullptr) {
		usedPath->clear();
	}
	for (const std::string& candidate : ConfigCandidates(configsDir)) {
		if (!FileExists(candidate)) {
			continue;
		}
		if (usedPath != nullptr) {
			*usedPath = candidate;
		}
		return LoadConfigFromFile(candidate);
	}
	return std::nullopt;
}

} // namespace advert
