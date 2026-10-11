# advertisement (Plugify)

Periodic chat, centre, alert, centre-HTML and console messages from configs/advertisement/advertisement.ini: every "update time" seconds the next entry of the "list" section is shown to everyone ("text" to chat with colour tags and \n line breaks, the legacy numeric "dest" still picks the sink, "center"/"hud"/"html"/"console" go to the centre of the screen, the alert line, the centre HTML panel and the console). The config is re-read on every map start. A port of the Metamod:Source plugin 'advertisement'.

Original plugin (not a port).

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, hudmenu (optional)

## Installation
Download `advertisement-2.0.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/advertisement`.

## License
GPL-3.0, see LICENSE.

