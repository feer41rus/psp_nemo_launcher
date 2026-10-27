# Thanks

**English** · [Русский](CREDITS.md)

Thanks to the projects Nemo Launcher is built on or learned from.

## Firmware and PSP launchers
- **ARK-5** — https://github.com/PSP-Arkfive/ARK-5 (GPL-3.0) — the custom firmware; `sctrl*` API, launching games, launcher mode, the NID table (`SystemControl/src/nid_660_data.c`), its plugin manager (`XMBControl/src/plugins.c`) as the model for ours. ARK's Russian XMB translation (`lang_ru.json` from `LANG.ARK`) is the base of our `lang_ru.json` with the "Nemo Launcher" name.
- **FasterARK** — https://github.com/PSP-Arkfive/FasterARK (no license stated) — ARK installer/updater; how the launcher and themes are installed; its "full" package is the base of the `full_with_ark-5` archive.
- **Launcher-Menus (arkMenu, xMenu, vshMenu)** — https://github.com/PSP-Arkfive/Launcher-Menus (GPL-3.0) — ARK's launcher; how to start ISO/PBP/PS1/homebrew, reading HOME/♪ through kubridge, the impose volume workaround (reference, no code copied).

## Network
- **WPA2 for PSP** by @Moment — built into ARK-5: `Compat/PSP/src/wpa2.c` (patch of `sceNetApctl_Library`, setting `always, wpa2, on`). It lets the PSP join WPA2-PSK (AES) routers.
- **How-to-connect-your-psp-to-the-internet** — https://github.com/stefanos-git/How-to-connect-your-psp-to-the-internet (no author or license stated) — guide and the `wpa2psp.prx` plugin; not included in Nemo — ARK-5 already has it built in.
- **libpspftp** — https://github.com/pspdev/libpspftp (MIT) — the PSP FTP server (`ftpd`) behind FTP in the Network tab.
- **PPSSPP** — https://github.com/hrydgard/ppsspp (GPL-2.0 or later) — NIDs of the network scan functions `sceNetApctlScanUser` / `GetBSSDescIDListUser` / `GetBSSDescEntryUser`, the Media Engine documentation (reference, no code copied).
- **Launcher-Menus / arkMenu** (GPL-3.0) — `network.cpp`, `net_mgr.cpp`, `usb.cpp`: network start-up order, starting FTP, USB mode (reference, no code copied).

## Overclocking
- **psp-undocumented-sorcery / experimental-overclock** by mcidclan — https://github.com/mcidclan/psp-undocumented-sorcery — clocks above 333 MHz (up to 443), built into ARK-5 (`Compat/PSP/src/overclock.c`); Nemo turns it on through ARK's `cpuclock:N` settings.

## Music
- **MP3PlayerPlugin** by plum_429 — https://github.com/PSP-Archive/MP3PlayerPlugin (GPL-3.0) — background music in the XMB and games; the base of the planned in-game music plugin (1.2).
- **vshbgm** by Koutsie — https://github.com/PSP-Archive/vshbgm (GPL-2.0) — background music in the VSH with `sceAudiocodec`.
- **BatterySteve** by koutsie — https://github.com/koutsie/BatterySteve (no licence stated) — the idea of testing the real battery capacity by draining it under load (no code copied).
- **pspmp3 / sceMp3** (PSPSDK, BSD) — https://github.com/pspdev/pspsdk — the hardware MP3 decoder (Media Engine).
- **dr_flac** by David Reid — https://github.com/mackron/dr_libs (public domain or MIT-0) — FLAC decoder (v0.12.42, copied from SDL_mixer 2.8.1); SDL_mixer in pspdev is built without FLAC.
- **The Mp3 Info Tag (LAME tag)** by Gabriel Bouvigne — http://gabriel.mp3-tech.org/mp3infotag.html — the Xing/Info and LAME tag format: frame count, seek TOC, encoder delay and padding for gapless playback.
- **VBRI header** (Fraunhofer) — reference for parsing (no code copied).

## Tools and libraries
- **pspdev / PSPSDK** — https://github.com/pspdev/pspdev (BSD) and the `pspdev/pspdev` Docker image — the PSP toolchain.
- **psp-cfw-sdk** — https://github.com/pspdev/psp-cfw-sdk — CFW headers and libraries (`systemctrl`, `kubridge`, `arkctrl`); PRO CFW / ARK code under GPL-3.0 or later — which is why Nemo Launcher is released under GPL-3.0-or-later.
- **SDL2, SDL2_mixer, SDL2_ttf, SDL2_image** — https://github.com/libsdl-org (zlib) — graphics, sound, fonts, images.
- **SDL2_gfx** — https://www.ferzkopp.net/wordpress/2016/01/02/sdl_gfx-sdl2_gfx/ (zlib) — primitives (rounded rectangles, circles).
- **cJSON** — https://github.com/DaveGamble/cJSON (MIT) — JSON for Nemo's data.
- **Inter** — https://github.com/rsms/inter (SIL OFL 1.1) — the interface font (cut down to Latin and Cyrillic).
