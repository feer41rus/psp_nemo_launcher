<p align="center"><img src="assets/branding/nemo_logo_readme.png" alt="Nemo Launcher" height="120"></p>

# Nemo Launcher

[English](README.md) · **Русский**

<p align="center"><img src="assets/branding/collage_ru.png" alt="Скриншоты Nemo Launcher" width="840"></p>

Современный лаунчер для PSP на кастомной прошивке **ARK-5**. Он заменяет «Custom Launcher» ARK (`VBOOT.PBP`), а сам ARK не трогает: игры, плагины и обновления остаются делом ARK, а Nemo делает для них удобный дом.

Сделано с ❤ Schastlivyy Vladislav вместе с **NCL — Nemo console league**.

## Возможности

- **Главная и Библиотека**: обложки и фоны из игр, «Продолжить», недавние, свои папки, значок «Мало места», когда на карте (на PSP Go — и во встроенной памяти) кончается место, экранная клавиатура (русская/английская). У «Библиотеки» свой фон под матовым покрытием: шесть картинок Nemo о выборе и возможностях (их смысл — в [assets/backgrounds/README.md](assets/backgrounds/README.md)) или своя из «Галереи» — «Настройки › Система › Nemo › Фон библиотеки».
- **Игры**: PSP ISO/CSO, PSN и homebrew EBOOT, PS1 EBOOT с объединением многодисковых игр. В карточке игры — формат и размер («ISO · 865 МБ»), **сохранения**, **дополнения (DLC)**, **индивидуальный разгон** и **удаление игры** (с переспросами).
- **Музыкальный плеер**: MP3 через **аппаратный декодер** PSP (мгновенное переключение, малая нагрузка), FLAC (16/24 бит, до 48 кГц), OGG и WAV программно; очередь, перемешивание/повтор, избранное, плейлисты, таймер сна, режим прослушивания с выключенным экраном, виды «Альбомы / Исполнители / Папки / Все треки» (кнопка □), импорт и экспорт плейлистов `.m3u`, 5-полосный эквалайзер с пресетами, ♪ / Select+L/R (у PSP Go и E1000 нет кнопки ♪).
- **Заставки и звуки**: при включении консоли — «Приветствую! Твоя PSP. Какой она должна быть.» со звуковым сигналом (пропуск любой кнопкой); для проданных консолей — разовая благодарность «Спасибо, что приобрели консоль у NCL» («Заставка NCL › При следующем запуске»); тихие звуки интерфейса в духе XMB, молчат при музыке. Всё настраивается в «Настройки › Система › Nemo».
- **Control Center**: в играх PSP и homebrew HOME открывает окно Nemo вместо окна Sony — к игре, скриншот, громкость, яркость, выход из игры; плагин ARK ставится и включается самим Nemo («Настройки › Система › Nemo › Control Center»). В менеджере плагинов — описание каждого плагина.
- **DLC и UMD**: «Дополнения (DLC)» в меню △ карточки игры: установка и удаление DLC — кладёте DLC в `PSP/NEMO/install_dlc`, Nemo раскладывает их в папку игры (DLC от другой версии игры — подгоняет с вашего согласия), помнит, что поставил, и удаляет выбранные — подробно в [инструкции по DLC](guides/dlc.ru.md); вставленный UMD сразу появляется на «Главной», START на плашке «UMD вставлен» запускает игру.
- **Сеть**: поиск и подключение к Wi‑Fi (WPA2 через ARK), **FTP-сервер**, **USB**-подключение; после них библиотека пересканируется сама. На PSP Go — раздел **Bluetooth** (пока только просмотр) и значок в строке, когда подключено устройство.
- **Настройки**, общие с XMB: дата и время (исправляет сбитые на 1970), форматы, часовой пояс, псевдоним, язык системы, яркость экрана (4 уровня, «Система › Яркость экрана»), автовыключение подсветки, автосон, кнопка подтверждения.
- **Настройки ARK** прямо в Nemo: зарядка по USB, память, ускорение карты, кэш Inferno, пропуск заставки Sony, светодиоды/UMD/мини-джойстик, регион UMD, **разгон** (игры / XMB / Nemo, до 443 МГц — эксперимент, на свой страх и риск), **менеджер плагинов**.
- **Аккумулятор** («Настройки › О PSP»): заряд, напряжение, температура, ёмкость и износ, циклы и серийный номер; тест реальной ёмкости с собственной нагрузкой и график результатов (на PSP Go и E1000 данные могут быть неточными).
- **Органайзер** (вкладка перед «Музыкой»): «Галерея», «Файловый менеджер», «Видео», «Камера» (Go!Cam: фото, видео со звуком, фильтры). Диски UMD Video и UMD Music играют в Nemo. **Файловый менеджер** — папки карты (и встроенной памяти PSP Go): копировать, переместить, удалить, переименовать, новая папка; редактор текста для конфигов с поиском. Вкладку можно скрыть: «Настройки › Система › Nemo».
- **Видео**: ролики из `VIDEO/` — фильмы MP4 для PSP (H.264 Baseline/Main до 480×272, AAC) аппаратно, видео с камер (AVI/MOV, Motion JPEG), диски UMD Video; субтитры .srt/.ass и встроенные (кириллица), «Продолжить с того же места».
- **Галерея**: все картинки из `PICTURE` и `DCIM` миниатюрами; во весь экран — листать, увеличивать, поворачивать (поворот сохраняется в файл, большие фото — по частям), удалять и делать фоном «Библиотеки» (△ › «Сделать фоном библиотеки»). Скриншоты из игр — в `PICTURE/Nemo/Games`, скриншоты Nemo — в `PICTURE/Nemo/Screenshots`.
- **Память** («Настройки › О PSP»): сколько свободно на карте; по кнопке — на что ушло место (игры ISO/CSO, игры и программы, DLC, сохранения, музыка, фото, видео, ARK и Nemo); подробности — файловая система карты, кластер, оперативная и внутренняя память, объём UMD. На PSP Go на первом месте встроенная память, переключение ←→.
- **Окно питания**: удержание HOME 1 с — сон, перезагрузка, выключение (и выход в XMB, если включён).
- **Nemo вместо XMB**: PSP включается сразу в Nemo; после двух неудачных запусков подряд режим выключается сам. Запасная копия (`PSP/APP/CUSTOM`, пункт XMB «Nemo Launcher (резерв)») восстанавливает Nemo, если обновление ARK его заменит.
- Русский и английский интерфейс, по языку системы.

## Управление

| Кнопки | Действие |
|---|---|
| ✕ / ○ | Выбрать / назад (какая из них «выбрать» — как в XMB, «Настройки › Система › Кнопка подтверждения») |
| Крестовина, мини-джойстик | Перемещение; ← → на полке листают игры |
| L / R | Предыдущая / следующая вкладка; в «Сейчас играет» и при выключенном экране (режим прослушивания) — предыдущий / следующий трек |
| △ | Меню действий (игра — в папку, удалить игру; папка; трек); в поле пароля — показать / скрыть символы |
| □ | Быстрое действие: на полке — в папку, в очереди — убрать трек, на клавиатуре — стереть символ |
| □ в «Музыке» | Вид медиатеки: Альбомы → Исполнители → Папки → Все треки; на экране эквалайзера — сброс |
| START | На клавиатуре — готово; в «Музыке» — «Сейчас играет» (во весь экран, ○ — назад к медиатеке); на плашке «UMD вставлен» — запустить диск |
| HOME (нажатие) | На «Главную» / закрыть окно |
| HOME (удержание 1 с) | Окно питания: сон, перезагрузка, выключение, выход в XMB |
| HOME в игре | Control Center: к игре, скриншот, громкость (← → или кнопки громкости), яркость, выход из игры |
| ♪ | Музыка: пауза / продолжить |
| ♪+L / ♪+R | Предыдущий / следующий трек |
| SELECT+L / SELECT+R | То же, для PSP Go и E1000 (у них нет ♪); при выключенном экране экран не включается |
| SELECT на «Сейчас играет» | Режим прослушивания (экран выключен); короткое SELECT — включить экран обратно |
| SELECT+START | Скриншот в `PICTURE/Nemo/Screenshots/` |
| SELECT+△ | Отладка: строка внизу и окошко RAM / CPU / FPS («Настройки › Система › Отладка») |
| Громкость + / − | Громкость (свой индикатор Nemo) |
| Пульт наушников | Пауза, следующий / предыдущий трек |
| L на экранной клавиатуре | Держать — заглавные буквы |

## Требования

- PSP (1000/2000/3000/Go/E1000) или PS Vita с **прошивкой 6.61** (или 6.60 — ниже, как обновиться) и **[ARK-5](https://github.com/PSP-Arkfive/ARK-5)**. На другом Nemo покажет сообщение и вернёт в XMB.

### Подготовка PSP: прошивка 6.61

ARK-5 (а значит и Nemo) нужна официальная системная прошивка **6.61** (6.60 тоже подходит). Посмотреть версию: XMB › *Настройки › Системные настройки › Информация о системе*. Официальные файлы обновления с сервера Sony (те же ссылки, что у DC10 из ARK-5):

- PSP 1000/2000/3000/E1000 (Street): [обновление 6.61](http://du01.psp.update.playstation.org/update/psp/image/us/2014_1212_6be8878f475ac5b1a499b95ab2f7d301/EBOOT.PBP)
- PSP Go: [обновление 6.61 для Go](http://du01.psp.update.playstation.org/update/psp/image2/us/2014_1212_fd0f7d0798b4f6e6d32ef95836740527/EBOOT.PBP)

**На официальной прошивке ниже 6.61:** положите обновление на карту как `PSP/GAME/UPDATE/EBOOT.PBP`, запустите его из XMB (*Игра › Карта памяти*) с заряженным аккумулятором и подключённой зарядкой, затем устанавливайте ARK-5.

**Если стоит PROMOD / PRO-C на 6.60–6.61:** пошаговая инструкция — [переход с PROMOD на ARK-5](guides/promod-to-ark5.ru.md).

**Если стоит другая кастомная прошивка (ME, LME, …):** поверх неё не обновляйте. Путь через [DC10](https://github.com/PSP-Arkfive/Despertar-Del-Cementerio) из архива `full`: переименуйте обновление в **`661.PBP`** (PSP Go: **`661GO.PBP`**) и положите в корень карты → запустите `Custom IPL` и установите новый cIPL → выключите PSP и включите с зажатой **L** → в DC10 выберите *Install 6.61 ARK*. Аккумулятор заряжена полностью, во время установки PSP не выключать.

## Установка

К каждому [релизу](https://github.com/feer41rus/psp_nemo_launcher/releases) приложены два архива:

**`…_lite_no_ark-5.zip`** — ARK-5 уже установлен:
1. На карте памяти откройте `PSP/SAVEDATA/ARK_01234/`. Если там есть `VBOOT.PBP`, переименуйте его в **`VBOOT_ARK.PBP`** (это лаунчер ARK; Nemo откроет его из «Настройки › Выход»).
2. Скопируйте в эту папку `VBOOT.PBP`, `lang_ru.json` и `lang_en.json` из архива.
3. Перезапустите XMB. Nemo запускается из пункта XMB **«Nemo Launcher»** (пункт лаунчера ARK).

**`…_full_with_ark-5.zip`** — ARK-5 (файлы PSP из официального «full»-пакета [FasterARK](https://github.com/PSP-Arkfive/FasterARK)) и уже установленный Nemo:
1. Скопируйте папку `PSP` в корень карты памяти (с объединением).
2. Установите/запустите ARK-5 по [инструкции ARK-5](https://github.com/PSP-Arkfive/ARK-5).

Обновление Nemo: заменить `VBOOT.PBP` (и оба `lang_*.json`).

### Если что-то пошло не так

- «Nemo вместо XMB» выключается сам после двух неудачных запусков подряд.
- Иначе на ПК (картридер или USB из XMB): удалите `PSP/SAVEDATA/ARK_01234/VBOOT.PBP` и переименуйте `VBOOT_ARK.PBP` обратно в `VBOOT.PBP` — получится ARK как был.

## Сборка

Тулчейн PSP работает в Docker:

```sh
make psp                   # dist/VBOOT.PBP + dist/lang_*.json (Docker-образ pspdev/pspdev)
make release CHANNEL=PROD  # dist/release/*.zip (нужны zip/unzip и FasterARK_psp_full.zip, см. Makefile)
```

## Каналы релизов

`PROD` — проверенные версии; `PRE-ALPHA` — сборки на тестировании; `DEV` — в разработке. Номер версии виден внизу «Настроек».

## Спасибо за наработки и разработки

<!-- credits:start -->
Спасибо за наработки и разработки проектам, на которых построен Nemo Launcher или у которых мы учились.

### Прошивка и лаунчеры PSP
- **ARK-5** — https://github.com/PSP-Arkfive/ARK-5 (GPL-3.0) — кастомная прошивка; API `sctrl*`, запуск игр, режим лаунчера, таблица NID (`SystemControl/src/nid_660_data.c`), менеджер плагинов (`XMBControl/src/plugins.c`) как образец. Русский перевод пунктов XMB ARK (`lang_ru.json` из `LANG.ARK`) — основа нашего `lang_ru.json` с именем «Nemo Launcher».
- **FasterARK** — https://github.com/PSP-Arkfive/FasterARK (лицензия не указана) — установщик/апдейтер ARK; разбор установки лаунчера и тем; пакет «full» — основа архива `full_with_ark-5`.
- **Launcher-Menus (arkMenu, xMenu, vshMenu)** — https://github.com/PSP-Arkfive/Launcher-Menus (GPL-3.0) — лаунчер ARK; как запускать ISO/PBP/PS1/homebrew, чтение HOME/♪ через kubridge, обход ошибки громкости impose (справочник, код не копировался).

### Сеть
- **WPA2 для PSP** от @Moment — встроен в ARK-5: `Compat/PSP/src/wpa2.c` (патч модуля `sceNetApctl_Library`, настройка `always, wpa2, on`). Благодаря ему PSP подключается к роутерам с WPA2-PSK (AES).
- **How-to-connect-your-psp-to-the-internet** — https://github.com/stefanos-git/How-to-connect-your-psp-to-the-internet (автор и лицензия не указаны) — инструкция и плагин `wpa2psp.prx`; в Nemo не включён — в ARK-5 то же самое уже встроено.
- **libpspftp** — https://github.com/pspdev/libpspftp (MIT) — FTP-сервер для PSP (`ftpd`), основа FTP во вкладке «Сеть».
- **PPSSPP** — https://github.com/hrydgard/ppsspp (GPL-2.0 или новее) — NID функций поиска сетей `sceNetApctlScanUser` / `GetBSSDescIDListUser` / `GetBSSDescEntryUser`, описание Media Engine (справочник, код не копировался).
- **Launcher-Menus / arkMenu** (GPL-3.0) — `network.cpp`, `net_mgr.cpp`, `usb.cpp`: порядок инициализации сети, запуск FTP, режим USB (справочник, код не копировался).

### Разгон
- **psp-undocumented-sorcery / experimental-overclock** от mcidclan — https://github.com/mcidclan/psp-undocumented-sorcery — разгон выше 333 МГц (до 443), встроен в ARK-5 (`Compat/PSP/src/overclock.c`); Nemo включает его через настройки ARK `cpuclock:N`.

### Музыка
- **MP3PlayerPlugin** от plum_429 — https://github.com/PSP-Archive/MP3PlayerPlugin (GPL-3.0) — фоновая музыка в XMB и играх; основа запланированного плагина музыки в играх (1.2).
- **vshbgm** от Koutsie — https://github.com/PSP-Archive/vshbgm (GPL-2.0) — пример фоновой музыки в VSH на `sceAudiocodec`.
- **pspmp3 / sceMp3** (PSPSDK, BSD) — https://github.com/pspdev/pspsdk — аппаратный декодер MP3 (Media Engine).
- **dr_flac** от David Reid — https://github.com/mackron/dr_libs (public domain или MIT-0) — декодер FLAC (v0.12.42, копия из SDL_mixer 2.8.1); SDL_mixer в pspdev собран без FLAC.
- **The Mp3 Info Tag (LAME tag)** от Gabriel Bouvigne — http://gabriel.mp3-tech.org/mp3infotag.html — формат Xing/Info и LAME-тега: число кадров, TOC для перемотки, задержка кодировщика и добивка для бесшовного воспроизведения.
- **Заголовок VBRI** (Fraunhofer) — справочник для разбора (код не копировался).

### Аккумулятор
- **BatterySteve** от koutsie — https://github.com/koutsie/BatterySteve (лицензия не указана) — идея теста реальной ёмкости аккумулятора разрядом под нагрузкой (код не копировался).

### DLC и UMD
- **ARK-5 / PRO CFW: NoDRM (Stargate)** — https://github.com/PSP-Arkfive/ARK-5 (GPL-3.0) — `Stargate/src/nodrm_patch.c` отдаёт играм расшифрованные DLC (`.edat`) без плагинов; Nemo только раскладывает файлы (справочник, код не копировался).
- **Launcher-Menus / arkMenu** (GPL-3.0) — `umd.cpp`: как запускать игру с диска UMD и с обновлением `PBOOT.PBP` (справочник, код не копировался).
- **RetroAchievements: Working with DLC for the PlayStation Portable** — https://docs.retroachievements.org/Working-with-DLC-for-the-PlayStation-Portable — где игры ищут DLC и почему ID должен совпадать с версией игры.
- **NPloader и NPdecrypter** (GameBrew) — https://www.gamebrew.org/wiki/NPloader_DLC_loader_PSP, https://gamebrew.org/wiki/NPdecrypter_PSP — как раньше подключали расшифрованные DLC плагином (справочник; в ARK-5 не нужны).

### Control Center (окно по HOME в играх)
- **CustomHOME** от ABCanG — https://www.gamebrew.org/wiki/CustomHOME_PSP (лицензия не указана) — идея своего меню по HOME в играх (код не копировался).
- **Custom Home Mod** от suloku — https://gamebrew.org/wiki/Custom_Home_Mod_PSP (лицензия не указана) — набор функций такого меню (код не копировался).
- **PSP-HUD** от ErikPshat — https://github.com/ErikPshat/PSP-HUD (лицензия не указана) — идея рисования поверх кадра игры через перехват вывода кадра (код не копировался).

### Инструменты и библиотеки
- **pspdev / PSPSDK** — https://github.com/pspdev/pspdev (BSD) и Docker-образ `pspdev/pspdev` — тулчейн PSP.
- **psp-cfw-sdk** — https://github.com/pspdev/psp-cfw-sdk — заголовки и библиотеки для CFW (`systemctrl`, `kubridge`, `arkctrl`); код PRO CFW / ARK под GPL-3.0 или новее — поэтому Nemo Launcher выпускается под GPL-3.0-or-later.
- **SDL2, SDL2_mixer, SDL2_ttf, SDL2_image** — https://github.com/libsdl-org (zlib) — графика, звук, шрифты, картинки.
- **SDL2_gfx** — https://www.ferzkopp.net/wordpress/2016/01/02/sdl_gfx-sdl2_gfx/ (zlib) — примитивы (скругления, окружности).
- **cJSON** — https://github.com/DaveGamble/cJSON (MIT) — JSON для данных Nemo.
- **libjpeg-turbo** — https://github.com/libjpeg-turbo/libjpeg-turbo (IJG + BSD-3-Clause + zlib) — «Фото»: быстрая распаковка JPEG с уменьшением, поворот без потерь, построчная запись больших фото.
- **libpng** — http://www.libpng.org/pub/png/libpng.html (libpng license) — «Фото»: построчное чтение и запись PNG при повороте больших фото.
- **PMP Mod AVC — avc.c** (Sorin P. C. «magiK», копия в openTri) — https://github.com/albe/openTri/blob/main/src/pmp/avc.c (GPL-2.0-or-later) — «Видео»: как подавать кадры H.264 аппаратному декодеру PSP (`sceMpegAvcDecode`, копирование кадра в Media Engine).
- **cooleyes — avcdecoder sample (PMPlayer Advance)** — https://archive.org/details/avcdecoder-3xx.-7z (GPL-2.0-or-later) — «Видео»: порядок вызовов для H.264 в MP4 через `mpeg_vsh` (`sceMpegGetAvcNalAu`, `sceMpegAvcDecodeDetail2`, `sceMpegBaseCscAvc`), NID этих функций, режимы Media Engine.
- **PPSSPP** — https://github.com/hrydgard/ppsspp (GPL-2.0-or-later) — справочник поведения `sceMpeg`/`sceJpeg` и находка: XMB запускает Media Engine в режиме 3 (`sceMeBootStart`, PR #22768).
- **LightMP3** (Sakya и др.) — https://wololo.net/talk/viewtopic.php?t=48108 (GPL) — NID `sceMeBootStart` для 6.60/6.61 (`0x5DFF5C50`).
- **Inter** — https://github.com/rsms/inter (SIL OFL 1.1) — шрифт интерфейса (урезан до латиницы и кириллицы) и растровый шрифт окна Control Center.
- **Pillow** — https://github.com/python-pillow/Pillow (MIT-CMU) и **NumPy** — https://github.com/numpy/numpy (BSD-3-Clause) — генератор фонов «Библиотеки» (`tools/make_library_bgs.py`): рисование, свечение, матовое покрытие.
- **«Господин Никто» (Mr. Nobody, реж. Жако Ван Дормель, 2009)** — идея имени Nemo и смысл фонов «Библиотеки»: выбор, возможности, альтернативные пути. Кадров из фильма в Nemo нет.
<!-- credits:end -->

**Бета-тестеры:** Schastlivyy Vladislav (feer41rus), Saint Nemo.

При создании Nemo Launcher применялся искусственный интеллект (Claude).

## Лицензия

Nemo Launcher — свободная программа под **GNU General Public License v3.0 или новее** — см. [LICENSE](LICENSE). Он собирается с GPL-3.0-кодом PRO CFW / ARK SDK (systemctrl, kubridge, библиотеки управления ARK), поэтому GPL-3.0 действует для всей программы. Встроенный сторонний код сохраняет свои лицензии: cJSON (MIT, `third_party/cjson`), libpspftp (MIT, `third_party/libpspftp`), шрифт Inter (SIL OFL 1.1, `assets/fonts/OFL.txt`).

Логотип NCL принадлежит NCL — Nemo console league. PSP и PlayStation — товарные знаки Sony Interactive Entertainment; проект не связан с Sony.
