// advertisement — the cs2-menus colour tags of the advertisement.ini texts.
//
// s2sdk's PrintToChat sends the message as it is and its PrintToChatColored uses a different tag
// table, so the {TAG} words the production advertisement.ini carries ({LIME}, {RED}, {GREEN},
// {OLIVE}, {BLUE}) are replaced here with exactly the control bytes the Metamod cs2-menus servers
// sent. The table is cs2-menus' utils.h colors_text[]/colors_hex[], byte for byte the same one
// pisexcompat carried and resetscore/vip_vips/as_rcon carry now; the last entry is the chat line
// break, which a chat message spells as U+2029 (the engine's line separator).
//
// Header-only and engine-free: tests/advertisement_tests.cpp checks every tag byte-for-byte.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace advert {
namespace detail {

struct ColorTag {
	std::string_view tag;
	std::string_view bytes;
};

inline constexpr std::array<ColorTag, 17> kColorTags = {{{"{DEFAULT}", "\x01"},
														 {"{WHITE}", "\x01"},
														 {"{RED}", "\x02"},
														 {"{LIGHTPURPLE}", "\x03"},
														 {"{GREEN}", "\x04"},
														 {"{LIME}", "\x05"},
														 {"{LIGHTGREEN}", "\x06"},
														 {"{DARKRED}", "\x07"},
														 {"{GRAY}", "\x08"},
														 {"{LIGHTOLIVE}", "\x09"},
														 {"{OLIVE}", "\x10"},
														 {"{LIGHTBLUE}", "\x0B"},
														 {"{BLUE}", "\x0C"},
														 {"{PURPLE}", "\x0E"},
														 {"{LIGHTRED}", "\x0F"},
														 {"{GRAYBLUE}", "\x0A"},
														 {"\\n", "\xe2\x80\xa9"}}};

} // namespace detail

// Replaces every cs2-menus tag with its chat control byte; unknown text is left alone.
inline std::string Colorize(std::string_view text) {
	std::string out(text);
	for (const detail::ColorTag& color : detail::kColorTags) {
		std::size_t pos = 0;
		while ((pos = out.find(color.tag, pos)) != std::string::npos) {
			out.replace(pos, color.tag.size(), color.bytes);
			pos += color.bytes.size();
		}
	}
	return out;
}

} // namespace advert
