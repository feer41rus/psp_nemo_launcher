# Благодарности

[English](CREDITS.en.md) · **Русский**

Спасибо за наработки и разработки проектам, на которых построен Nemo Launcher или у которых мы учились.

## Прошивка и лаунчеры PSP
- **ARK-5** — https://github.com/PSP-Arkfive/ARK-5 (GPL-3.0) — кастомная прошивка; API `sctrl*`, запуск игр, режим лаунчера, таблица NID (`SystemControl/src/nid_660_data.c`), менеджер плагинов (`XMBControl/src/plugins.c`) как образец. Русский перевод пунктов XMB ARK (`lang_ru.json` из `LANG.ARK`) — основа нашего `lang_ru.json` с именем «Nemo Launcher».
- **FasterARK** — https://github.com/PSP-Arkfive/FasterARK (лицензия не указана) — установщик/апдейтер ARK; разбор установки лаунчера и тем; пакет «full» — основа архива `full_with_ark-5`.
- **Launcher-Menus (arkMenu, xMenu, vshMenu)** — https://github.com/PSP-Arkfive/Launcher-Menus (GPL-3.0) — лаунчер ARK; как запускать ISO/PBP/PS1/homebrew, чтение HOME/♪ через kubridge, обход ошибки громкости impose (справочник, код не копировался).

## Сеть
- **WPA2 для PSP** от @Moment — встроен в ARK-5: `Compat/PSP/src/wpa2.c` (патч модуля `sceNetApctl_Library`, настройка `always, wpa2, on`). Благодаря ему PSP подключается к роутерам с WPA2-PSK (AES).
- **How-to-connect-your-psp-to-the-internet** — https://github.com/stefanos-git/How-to-connect-your-psp-to-the-internet (автор и лицензия не указаны) — инструкция и плагин `wpa2psp.prx`; в Nemo не включён — в ARK-5 то же самое уже встроено.
- **libpspftp** — https://github.com/pspdev/libpspftp (MIT) — FTP-сервер для PSP (`ftpd`), основа FTP во вкладке «Сеть».
- **PPSSPP** — https://github.com/hrydgard/ppsspp (GPL-2.0 или новее) — NID функций поиска сетей `sceNetApctlScanUser` / `GetBSSDescIDListUser` / `GetBSSDescEntryUser`, описание Media Engine (справочник, код не копировался).
- **Launcher-Menus / arkMenu** (GPL-3.0) — `network.cpp`, `net_mgr.cpp`, `usb.cpp`: порядок инициализации сети, запуск FTP, режим USB (справочник, код не копировался).

## Разгон
- **psp-undocumented-sorcery / experimental-overclock** от mcidclan — https://github.com/mcidclan/psp-undocumented-sorcery — разгон выше 333 МГц (до 443), встроен в ARK-5 (`Compat/PSP/src/overclock.c`); Nemo включает его через настройки ARK `cpuclock:N`.

## Музыка
- **MP3PlayerPlugin** от plum_429 — https://github.com/PSP-Archive/MP3PlayerPlugin (GPL-3.0) — фоновая музыка в XMB и играх; основа запланированного плагина музыки в играх (1.2).
- **vshbgm** от Koutsie — https://github.com/PSP-Archive/vshbgm (GPL-2.0) — пример фоновой музыки в VSH на `sceAudiocodec`.
- **pspmp3 / sceMp3** (PSPSDK, BSD) — https://github.com/pspdev/pspsdk — аппаратный декодер MP3 (Media Engine).
- **The Mp3 Info Tag (LAME tag)** от Gabriel Bouvigne — http://gabriel.mp3-tech.org/mp3infotag.html — формат Xing/Info и LAME-тега: число кадров, TOC для перемотки, задержка кодировщика и добивка для бесшовного воспроизведения.
- **Заголовок VBRI** (Fraunhofer) — справочник для разбора (код не копировался).

## Инструменты и библиотеки
- **pspdev / PSPSDK** — https://github.com/pspdev/pspdev (BSD) и Docker-образ `pspdev/pspdev` — тулчейн PSP.
- **psp-cfw-sdk** — https://github.com/pspdev/psp-cfw-sdk — заголовки и библиотеки для CFW (`systemctrl`, `kubridge`, `arkctrl`); код PRO CFW / ARK под GPL-3.0 или новее — поэтому Nemo Launcher выпускается под GPL-3.0-or-later.
- **SDL2, SDL2_mixer, SDL2_ttf, SDL2_image** — https://github.com/libsdl-org (zlib) — графика, звук, шрифты, картинки.
- **SDL2_gfx** — https://www.ferzkopp.net/wordpress/2016/01/02/sdl_gfx-sdl2_gfx/ (zlib) — примитивы (скругления, окружности).
- **cJSON** — https://github.com/DaveGamble/cJSON (MIT) — JSON для данных Nemo.
- **Inter** — https://github.com/rsms/inter (SIL OFL 1.1) — шрифт интерфейса (урезан до латиницы и кириллицы).
