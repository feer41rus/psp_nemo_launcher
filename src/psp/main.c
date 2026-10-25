// Nemo Launcher: replacement for ARK's custom launcher (VBOOT.PBP).
#include <pspkernel.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "art.h"
#include "buttons.h"
#include "gfx.h"
#include "launch.h"
#include <psphprm.h>
#include "log.h"
#include "pad.h"
#include "power.h"
#include "platform.h"
#include "scanner.h"
#include "sysreg.h"
#include "ui.h"
#include "version.h"
#include "player.h"
#include "device.h"
#include "usb.h"
#include <kubridge.h>
#include <systemctrl.h>
#include <pspsysmem.h>
#include <systemctrl_se.h>
#include "require.h"

PSP_MODULE_INFO("NemoLauncher", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
// Heap = all free memory minus the threshold. Leave 3 MB to the system: the Wi-Fi modules and
// their 256 KB pool are loaded at run time (with the SDK default ~1 MB sceNetInit failed with
// 0x80020190, out of memory). Current PSPSDK ignores the value of a negative HEAP_SIZE.
PSP_HEAP_SIZE_KB(-1);
PSP_HEAP_THRESHOLD_SIZE_KB(3072);

// No exit callback on purpose: HOME must reach Nemo instead of the system "quit" dialog.

static App app;

static void rebuild_shelves(App *a) {
    a->home_count = shelves_home(&a->games, a->lib, a->home, NAV_MAX_SHELVES, &a->continue_game);
    a->library_count = shelves_library(&a->games, a->lib, a->library, NAV_MAX_SHELVES);
}

void media_suspend(App *a) {
    if (a->media_suspended) return;
    music_save_state(a);
    player_stop();
    art_reset();
    a->media_suspended = 1;
    platform_log_memory("media-suspended");
}

void media_resume(App *a) {
    if (!a->media_suspended) return;
    a->media_suspended = 0;
    library_rescan(a);
    platform_log_memory("media-resumed");
}

void library_rescan(App *a) {
    uint32_t t0 = platform_ms();
    int games_before = a->games.count, tracks_before = a->mlib.track_count;
    gamelist_free(&a->games);
    gamelist_init(&a->games);
    ScanStats gs = { 0, 0 };
    scan_games_cached("ms0:", "ms0:/PSP/NEMO/scan.json", &a->games, &gs);
    rebuild_shelves(a);
    music_reload(a);
    art_reset();
    log_msg("rescan: %d games (was %d), %d tracks (was %d) in %u ms", a->games.count, games_before,
            a->mlib.track_count, tracks_before, (unsigned)(platform_ms() - t0));
    char msg[128];
    snprintf(msg, sizeof msg, tr(S_RESCAN_DONE), a->games.count, a->mlib.track_count);
    ui_toast(a, msg);
}

static void build_model(App *a) {
    NavModel *m = &a->model;
    memset(m, 0, sizeof *m);
    m->settings_count = SET_COUNT;
    const Shelf *shelves = NULL;
    int count = 0;
    if (a->nav.tab == TAB_HOME) {
        shelves = a->home;
        count = a->home_count;
        m->has_continue = a->continue_game >= 0;
    } else if (a->nav.tab == TAB_LIBRARY) {
        shelves = a->library;
        count = a->library_count;
    }
    m->shelf_count = count;
    for (int i = 0; i < count && i < NAV_MAX_SHELVES; i++) m->shelf_len[i] = shelves[i].count;
    if (a->nav.card_game >= 0 && a->nav.card_game < a->games.count)
        m->card_discs = a->games.items[a->nav.card_game].disc_count;
    m->menu_count = a->menu_count;
    m->pick_count = a->lib->folder_count + 1;   // folders + "New folder…"
    if (!a->lib->settings.show_music) m->hidden_tabs |= 1u << TAB_MUSIC;
}

static void open_card_for(App *a, int game) {
    if (game < 0) { nav_pop(&a->nav); return; }
    const Game *g = &a->games.items[game];
    a->nav.card_game = game;
    int last = library_last_disc(a->lib, g->id);
    a->nav.card_disc = (g->disc_count > 1 && last >= 1 && last <= g->disc_count) ? last : 1;
    a->card_opened_ms = a->now_ms;
    log_msg("card: game=%d \"%s\"", game, g->title);
}

static void open_card(App *a) { open_card_for(a, ui_focused_game(a, a->nav.tab)); }

// Folder index of the focused shelf when it is a user folder shelf, -1 otherwise.
static int focused_folder(const App *a) {
    const Shelf *shelves = a->nav.tab == TAB_HOME ? a->home : a->library;
    int count = a->nav.tab == TAB_HOME ? a->home_count : a->library_count;
    int s = a->nav.shelf[a->nav.tab];
    if (s < 0 || s >= count || shelves[s].kind != SHELF_FOLDER) return -1;
    return shelves[s].folder_idx;
}

static void save_library(App *a) {
    library_save(a->lib, NEMO_DATA_PATH);
    rebuild_shelves(a);
}

static void open_menu(App *a) {
    a->menu_game = ui_focused_game(a, a->nav.tab);
    a->menu_folder = focused_folder(a);
    a->menu_count = 0;
    if (a->menu_game >= 0) {   // "Details" was dropped: it duplicated X (open card)
        a->menu[a->menu_count++] = MI_TO_FOLDER;
        if (a->menu_folder >= 0) a->menu[a->menu_count++] = MI_REMOVE_FROM_FOLDER;
    }
    if (a->menu_folder >= 0) {
        a->menu[a->menu_count++] = MI_RENAME_FOLDER;
        a->menu[a->menu_count++] = MI_DELETE_FOLDER;
    }
    if (a->menu_count == 0) nav_pop(&a->nav);
}

static void open_osk(App *a, OskPurpose purpose, int folder) {
    a->osk_purpose = purpose;
    a->osk_folder = folder;
    osk_init(&a->osk, purpose == OSK_FOR_RENAME ? a->lib->folders[folder].name : "");
    nav_push(&a->nav, SCR_OSK);
}

static void menu_pick(App *a, int index) {
    if (index < 0 || index >= a->menu_count) return;
    nav_pop(&a->nav);                          // close the menu; actions may open something else
    switch (a->menu[index]) {
    case MI_TO_FOLDER:
        a->pick_game = a->menu_game;
        nav_push(&a->nav, SCR_FOLDER_PICK);
        break;
    case MI_REMOVE_FROM_FOLDER:
        library_folder_remove(a->lib, a->menu_folder, a->games.items[a->menu_game].id);
        save_library(a);
        break;
    case MI_DETAILS:
        nav_push(&a->nav, SCR_CARD);
        open_card_for(a, a->menu_game);
        break;
    case MI_RENAME_FOLDER:
        open_osk(a, OSK_FOR_RENAME, a->menu_folder);
        break;
    case MI_DELETE_FOLDER:
        nav_push(&a->nav, SCR_CONFIRM);
        break;
    }
}

static void folder_pick(App *a, int index) {
    const char *id = a->games.items[a->pick_game].id;
    if (index == a->lib->folder_count) {       // "New folder…"
        open_osk(a, OSK_FOR_NEW_FOLDER, -1);
        return;
    }
    if (library_folder_has(a->lib, index, id)) library_folder_remove(a->lib, index, id);
    else library_folder_add(a->lib, index, id);
    save_library(a);
}

static void osk_input(App *a, Action act) {
    OskState st = osk_handle(&a->osk, act);
    if (st == OSK_CANCEL) { nav_pop(&a->nav); return; }
    if (st != OSK_DONE) return;
    int ok;
    if (a->osk_purpose == OSK_FOR_RENAME) {
        ok = library_folder_rename(a->lib, a->osk_folder, a->osk.text) == 0;
    } else {
        int idx = library_folder_create(a->lib, a->osk.text);
        ok = idx >= 0;
        if (ok && a->pick_game >= 0) library_folder_add(a->lib, idx, a->games.items[a->pick_game].id);
    }
    if (!ok) { ui_toast(a, tr(S_ERR_FOLDER_NAME)); return; }
    log_msg("folders: %s \"%s\"", a->osk_purpose == OSK_FOR_RENAME ? "rename" : "create", a->osk.text);
    save_library(a);
    nav_pop(&a->nav);
}

static void start_game(App *a) {
    const Game *g = &a->games.items[a->nav.card_game];
    int disc = g->disc_count > 1 ? a->nav.card_disc : 0;
    if (g->kind != GK_UTILITY) {
        library_touch_recent(a->lib, g->id, disc);
        library_save(a->lib, NEMO_DATA_PATH);
    }
    music_save_state(a);   // the game reboots the console: keep the music position
    net_shutdown();
    selfguard_clean_exit();   // a quick game launch is not a failed Nemo start
    log_msg("launch: \"%s\" format=%d disc=%d path=%s", g->title, g->format, disc, g->path);
    int rc = launch_game(g, disc);
    log_msg("launch: failed rc=0x%08x", (unsigned)rc);
    platform_set_launcher_mode(a->lib->settings.replace_xmb);   // the failed launch cleared it
    if (rc < 0) {
        ui_toast(a, tr(S_ERR_LAUNCH));
        rebuild_shelves(a);
    }
}

// Restart / turn off: keep the music place and the library, stop FTP and Wi-Fi first.
static void power_prepare(App *a, const char *what) {
    log_msg("power: %s from the power window", what);
    music_save_state(a);
    library_save(a->lib, NEMO_DATA_PATH);
    if (a->ftp.phase != FTP_OFF) ftpsrv_disable();
    net_shutdown();
    selfguard_clean_exit();
}

void power_do(App *a, int item) {
    switch (item) {
    case PI_SLEEP:
        music_save_state(a);
        power_sleep();
        break;
    case PI_REBOOT: power_prepare(a, "restart"); power_reboot(); break;
    case PI_SHUTDOWN: power_prepare(a, "shutdown"); power_shutdown(); break;
    case PI_EXIT_XMB: settings_exit_to_xmb(a); break;
    default: break;
    }
}

// HOME held for 3 s: open (or close) the power window. Not on the USB screen: the PC holds the stick.
static void home_hold(App *a) {
    if (a->power.open) a->power.open = 0;
    else if (a->net_screen != NETS_USB) powermenu_open(&a->power, a->lib->settings.xmb_exit);
}

static void handle_action(App *a, Action act) {
    if (a->listening) {                       // listening mode: every button (even HOME) only wakes
        music_action(a, act);
        return;
    }
    if (a->power.open) {                      // the power window owns every button
        int item = powermenu_handle(&a->power, act);
        if (item >= 0) power_do(a, item);
        return;
    }
    if (a->clock_prompt) {                    // "clock was reset" question owns every button
        settings_clock_prompt_action(a, act);
        return;
    }
    if (a->rx_stage) { selfguard_action(a, act); return; }   // "ARK replaced Nemo, restore?"
    if (act == ACT_HOME) {                    // HOME leaves for Home: nothing may stay open on top
        while (a->page) textpage_action(a, ACT_BACK);
        if (a->saves) saves_action(a, ACT_BACK);
        a->gm_open = 0;
    }
    if (a->page && act != ACT_HOME) { textpage_action(a, act); return; }
    if (a->saves && act != ACT_HOME) { saves_action(a, act); return; }
    if (nav_top(&a->nav) == SCR_CARD && act != ACT_HOME) {   // game card: △ = game menu
        if (a->gm_open) { gamemenu_action(a, act); return; }
        if (act == ACT_MENU) { gamemenu_open(a); return; }
    }
    // Settings: their own navigation; L/R/HOME stay global (the date editor keeps L/R too).
    if (a->nav.tab == TAB_SETTINGS && nav_top(&a->nav) == SCR_ROOT &&
        (a->dt_open || a->nick_open ? act != ACT_HOME : (act != ACT_TAB_PREV && act != ACT_TAB_NEXT && act != ACT_HOME))) {
        settings_action(a, act);
        return;
    }
    // The Music tab has its own navigation; tabs (L/R) and HOME stay global, except while the
    // playlist keyboard or listening mode is open (they own every button).
    if (a->nav.tab == TAB_MUSIC && nav_top(&a->nav) == SCR_ROOT) {
        MusicScreen mt = mnav_top(&a->mnav);
        int owns_all = mt == MS_OSK || mt == MS_LISTEN;
        if (owns_all ? act != ACT_HOME : (act != ACT_TAB_PREV && act != ACT_TAB_NEXT && act != ACT_HOME)) {
            music_action(a, act);
            return;
        }
    }
    // The USB screen owns even HOME: leaving it any other way would read the stick the PC still holds.
    if (a->nav.tab == TAB_NETWORK && nav_top(&a->nav) == SCR_ROOT &&
        (a->net_screen == NETS_USB ||
         (network_owns_all(a) ? act != ACT_HOME : (act != ACT_TAB_PREV && act != ACT_TAB_NEXT && act != ACT_HOME)))) {
        network_action(a, act);
        return;
    }
    if (nav_top(&a->nav) == SCR_OSK && act != ACT_HOME) {   // the keyboard owns all other input
        osk_input(a, act);
        return;
    }
    NavCmd c = nav_handle(&a->nav, act, &a->model);
    switch (c.type) {
    case CMD_OPEN_CARD: open_card(a); break;
    case CMD_LAUNCH: start_game(a); break;
    case CMD_OPEN_MENU: open_menu(a); break;
    case CMD_MENU_PICK: menu_pick(a, c.index); break;
    case CMD_OPEN_PICK:
        a->pick_game = ui_focused_game(a, a->nav.tab);
        if (a->pick_game < 0) nav_pop(&a->nav);
        break;
    case CMD_FOLDER_PICK: folder_pick(a, c.index); break;
    case CMD_CONFIRM_YES:
        log_msg("folders: delete \"%s\"", a->lib->folders[a->menu_folder].name);
        library_folder_delete(a->lib, a->menu_folder);
        save_library(a);
        break;
    default: break;
    }
}

static void draw_hints(App *a) {
    ScreenKind top = nav_top(&a->nav);
    if (a->clock_prompt) {
        settings_draw_hints(a);
    } else if (a->page) {
        static const Hint h[] = { { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 1);
    } else if (a->saves) {
        saves_draw_hints(a);
    } else if (top == SCR_CARD && a->gm_open) {
        gamemenu_draw_hints(a);
    } else if (top == SCR_OSK) {
        static const Hint h[] = { { ACT_CONFIRM, S_OSK_HINT_TYPE }, { ACT_QUICK, S_OSK_HINT_DEL },
                                  { ACT_TAB_PREV, S_OSK_HINT_CAPS }, { ACT_BACK, S_OSK_HINT_CANCEL },
                                  { ACT_START, S_OSK_HINT_DONE } };
        ui_hints_draw(a, h, 5);
    } else if (top == SCR_GAME_MENU || top == SCR_FOLDER_PICK || top == SCR_CONFIRM) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (top == SCR_CARD) {
        const Game *g = &a->games.items[a->nav.card_game];
        Hint h[4] = { { ACT_CONFIRM, g->kind == GK_UTILITY ? S_RUN_UTILITY : S_HINT_PLAY },
                      { ACT_MENU, S_HINT_MENU }, { ACT_BACK, S_HINT_BACK }, { ACT_LEFT, S_HINT_DISC } };
        ui_hints_draw(a, h, g->disc_count > 1 ? 4 : 3);
    } else if (a->nav.tab == TAB_HOME || a->nav.tab == TAB_LIBRARY) {
        // L/R are shown in the tab bar already, so no "Sections" hint here.
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_OPEN }, { ACT_MENU, S_HINT_MENU },
                                  { ACT_QUICK, S_HINT_TO_FOLDER } };
        ui_hints_draw(a, h, 3);
    } else if (a->nav.tab == TAB_MUSIC) {
        music_draw_hints(a);
    } else if (a->nav.tab == TAB_NETWORK) {
        network_draw_hints(a);
    } else if (a->nav.tab == TAB_SETTINGS) {
        settings_draw_hints(a);
    } else {
        ui_hints_draw(a, NULL, 0);
    }
}

static void draw_debug(App *a) {
    SDL_RendererInfo info;
    SDL_GetRendererInfo(gfx_renderer(), &info);
    SDL_Window *w = SDL_RenderGetWindow(gfx_renderer());
    char line[160];
    snprintf(line, sizeof line, "%s | win %s | tex0 %s | %d fps | games %d",
             info.name, SDL_GetPixelFormatName(SDL_GetWindowPixelFormat(w)),
             info.num_texture_formats ? SDL_GetPixelFormatName(info.texture_formats[0]) : "-",
             a->fps, a->games.count);
    gfx_rect(0, SCREEN_H - HINT_BAR_H - 16, SCREEN_W, 16, 0x000000c0);
    gfx_text(FONT_SMALL, 4, SCREEN_H - HINT_BAR_H - 15, 0x5fe0b0ff, line);
}

#define SHOT_DIR "ms0:/PSP/NEMO/screenshots"

static void take_screenshot(App *a) {
    if (usb_active()) return;                 // the memory stick belongs to the PC
    mkdir(SHOT_DIR, 0777);
    char path[96];
    struct stat st;
    for (int i = 1; i < 1000; i++) {
        snprintf(path, sizeof path, SHOT_DIR "/nemo_%03d.png", i);
        if (stat(path, &st) != 0) break;
    }
    int rc = gfx_screenshot(path);
    log_msg("screenshot: %s rc=%d", path, rc);
    a->now_ms = platform_ms();   // saving the PNG takes ~2 s: time the toast from now, not frame start
    if (rc == 0) ui_toast(a, tr(S_SCREENSHOT_SAVED));
}

static void draw(App *a) {
    gfx_frame_begin();
    int card_below = 0;
    for (int i = 0; i < a->nav.depth; i++) if (a->nav.stack[i].kind == SCR_CARD) card_below = 1;
    if (a->page) {
        textpage_draw(a);
    } else if (a->saves) {
        saves_draw(a);
    } else if (card_below) {
        ui_card_draw(a);
        if (a->gm_open) gamemenu_draw(a);
    } else {
        switch (a->nav.tab) {
        case TAB_HOME: ui_shelves_draw(a, TAB_HOME); break;
        case TAB_LIBRARY: ui_shelves_draw(a, TAB_LIBRARY); break;
        case TAB_MUSIC: ui_music_draw(a); break;
        case TAB_NETWORK: ui_network_draw(a); break;
        case TAB_SETTINGS: ui_settings_draw(a); break;
        default: break;
        }
    }
    switch (nav_top(&a->nav)) {
    case SCR_GAME_MENU: ui_menu_draw(a); break;
    case SCR_FOLDER_PICK: ui_folder_pick_draw(a); break;
    case SCR_CONFIRM: ui_confirm_draw(a); break;
    case SCR_OSK: ui_osk_draw(a); break;
    default: break;
    }
    if (!network_fullscreen(a)) ui_chrome_draw(a);
    draw_hints(a);
    settings_draw_overlays(a);
    selfguard_draw(a);
    if (a->power.open) ui_power_draw(a);
    else ui_home_bar_draw(a);
    if (a->debug) draw_debug(a);
    if (a->screenshot) {   // capture a frame without toasts/overlays on top
        a->screenshot = 0;
        gfx_frame_end();
        take_screenshot(a);
        return;
    }
    ui_toast_draw(a);
    ui_volume_draw(a);
    gfx_frame_end();
}

// Show our own volume indicator; also undo impose's occasional opposite-direction step
// (same workaround as arkMenu/src/system_mgr.cpp).
static void handle_volume(App *a, uint32_t pressed) {
    if (!(pressed & (NB_VOLUP | NB_VOLDOWN))) return;
    int v = platform_volume_get();
    if (v < 0) return;
    if (a->volume >= 0) {
        if ((pressed & NB_VOLUP) && v < a->volume) platform_volume_set(++v);
        else if ((pressed & NB_VOLDOWN) && v > a->volume) platform_volume_set(--v);
    }
    if (v != a->volume) log_msg("volume: %d -> %d", a->volume, v);
    a->volume = v;
    a->volume_until = a->now_ms + 1500;
}

static void log_renderer(void) {
    SDL_RendererInfo info;
    SDL_GetRendererInfo(gfx_renderer(), &info);
    SDL_Window *w = SDL_RenderGetWindow(gfx_renderer());
    log_msg("gfx: renderer=%s flags=0x%x window=%s", info.name, (unsigned)info.flags,
            SDL_GetPixelFormatName(SDL_GetWindowPixelFormat(w)));
    for (unsigned i = 0; i < info.num_texture_formats; i++)
        log_msg("gfx: texture format %u = %s", i, SDL_GetPixelFormatName(info.texture_formats[i]));
}

// Firmware 6.60+ with ARK-5 only (author, 1.1.8): otherwise a message and the way back to the XMB.
static void require_or_exit(App *a) {
    char fw[16];
    int ark = sctrlSEGetVersion();
    ReqResult r = require_check((uint32_t)sceKernelDevkitVersion(), ark, fw, sizeof fw);
    log_msg("require: fw %s, ARK major %d -> %d", fw, ark, (int)r);
    if (r == REQ_OK) return;
    char line[128];
    if (r == REQ_OLD_FIRMWARE) snprintf(line, sizeof line, tr(S_REQ_FW_FMT), fw);
    else snprintf(line, sizeof line, "%s", tr(S_REQ_ARK));
    uint32_t prev = 0;
    for (;;) {
        uint8_t lx, ly;
        uint32_t b = pad_read(&lx, &ly), pressed = b & ~prev;
        prev = b;
        if (pressed & (NB_CROSS | NB_CIRCLE | NB_START | NB_HOME)) {
            log_msg("require: exit to XMB");
            sctrlKernelExitVSH(NULL);
        }
        gfx_frame_begin();
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, COL_BG);
        gfx_text(FONT_TITLE, 24, 60, COL_WARN, tr(S_REQ_TITLE));
        gfx_text_fit(FONT_BODY, 24, 100, SCREEN_W - 48, COL_TEXT, line);
        gfx_text_fit(FONT_SMALL, 24, 130, SCREEN_W - 48, COL_MUTED, tr(S_REQ_WHERE));
        static const Hint h[] = { { ACT_CONFIRM, S_REQ_EXIT } };
        ui_hints_draw(a, h, 1);
        gfx_frame_end();
    }
}

static void draw_loading(App *a) {
    gfx_frame_begin();
    ui_stub_draw(a, S_TAB_HOME, S_LOADING);
    ui_chrome_draw(a);
    gfx_frame_end();
}

int main(int argc, char *argv[]) {
    SDL_SetMainReady();
    platform_init();
    log_open();
    log_msg("Nemo Launcher %s (%s %s) start, arkpath=%s", NEMO_VERSION, __DATE__, __TIME__, platform_arkpath());
    {
        int model = platform_model();
        char gen[8];
        device_gen_string(model, gen, sizeof gen);
        log_msg("device: %s (%s, model=%d), fw %s, ARK %08X.%08X, wifi=%d", device_model_name(model), gen, model,
                platform_fw(), (unsigned)sctrlHENGetVersion(), (unsigned)sctrlHENGetMinorVersion(),
                device_has_wifi(model));
    }
    pad_init();
    if (gfx_init() != 0) {
        log_msg("gfx_init failed: %s", SDL_GetError());
        sceKernelExitGame();
        return 1;
    }

    log_renderer();
    settings_load_language();   // the requirement message speaks the system language
    require_or_exit(&app);
    platform_log_memory("start");
    app.lib = malloc(sizeof *app.lib);
    int lib_rc = library_load(app.lib, NEMO_DATA_PATH);
    if (lib_rc < 0) {   // never overwrite the user's folders with an empty library
        remove(NEMO_DATA_PATH ".bad");
        rename(NEMO_DATA_PATH, NEMO_DATA_PATH ".bad");
    }
    log_msg("library: load rc=%d folders=%d recent=%d", lib_rc, app.lib->folder_count, app.lib->recent_count);
    // "launcher, on" stays only for Nemo instead of XMB (a game launch may have left it on).
    selfguard_start(&app, argc > 0 ? argv[0] : "");
    settings_load_language();   // Nemo follows the system language (1.1.5)
    nav_init(&app.nav);
    app.nav.card_game = -1;
    app.menu_game = app.menu_folder = app.pick_game = -1;
    int cross = sysreg_get_confirm_cross();
    app.registry_ok = cross >= 0;
    app.confirm_cross = cross != 0;   // error -> cross (Western default)
    app.volume = platform_volume_get();
    log_msg("registry: button_assign=%d", cross);

    log_msg("ui: first frame");
    draw_loading(&app);
    music_init(&app, app.lib->settings.show_music);
    network_init(&app);
    settings_init(&app);
    log_msg("scan: start");
    gamelist_init(&app.games);
    uint32_t scan_start = platform_ms();
    ScanStats scan_stats = { 0, 0 };
    scan_games_cached("ms0:", "ms0:/PSP/NEMO/scan.json", &app.games, &scan_stats);
    rebuild_shelves(&app);
    log_msg("scan: %d games in %u ms (parsed %d, cached %d), home shelves=%d continue=%d library shelves=%d",
            app.games.count, (unsigned)(platform_ms() - scan_start), scan_stats.parsed, scan_stats.reused,
            app.home_count, app.continue_game, app.library_count);

    InputRepeat repeat = { 0, 0 };
    uint32_t prev = 0, fps_start = platform_ms();
    int frames = 0;
    for (;;) {
        app.now_ms = platform_ms();
        uint8_t lx, ly;
        uint32_t buttons = pad_read(&lx, &ly);
        uint32_t dirs = (buttons & NB_DIRS) | input_stick_dirs(lx, ly);
        uint32_t pressed = (buttons & ~prev & ~NB_DIRS) | input_repeat(&repeat, dirs, app.now_ms);
        prev = buttons;
        if (pressed && powersave_press(&app.psave, app.now_ms)) pressed = 0;   // woke the backlight (XMB auto-off)
        if (pressed && idle_press(&app.idle, app.now_ms)) pressed = 0;   // this press only woke the screen
        // Listening mode keeps HOME as a plain press: it only wakes, never starts a hold.
        HomeEvent home = app.listening ? HOME_NONE : homekey_update(&app.home_key, buttons, &pressed, app.now_ms);
        handle_volume(&app, pressed);
        NoteEvent note = notekey_update(&app.note, buttons, &pressed);   // ♪, ♪+L, ♪+R, SELECT+L/R
        music_note(&app, note);
        u32 hprm = 0;
        if (sceHprmPeekCurrentKey(&hprm) >= 0) music_hprm(&app, hprm);
        // SELECT is a modifier: SELECT+START = screenshot, SELECT+TRIANGLE = debug line.
        // SELECT alone (released without a combo) on Now Playing = listening mode.
        static int select_held, select_used;
        if (app.listening && (pressed & NB_SELECT)) music_action(&app, ACT_NONE);   // SELECT wakes too
        if (buttons & NB_SELECT) {
            if (!select_held) { select_held = 1; select_used = 0; }
            if (note == NOTE_PREV || note == NOTE_NEXT) select_used = 1;   // no listening mode after SELECT+L/R
            if ((buttons & NB_START) && (pressed & (NB_START | NB_SELECT))) { app.screenshot = 1; select_used = 1; }
            if (pressed & NB_TRIANGLE) { app.debug = !app.debug; select_used = 1; }
            pressed = 0;
        } else if (select_held) {
            select_held = 0;
            if (!select_used && app.nav.tab == TAB_MUSIC && mnav_top(&app.mnav) == MS_NOW)
                music_enter_listening(&app);
        }

        int osk_open = nav_top(&app.nav) == SCR_OSK || (app.nav.tab == TAB_MUSIC && mnav_top(&app.mnav) == MS_OSK) ||
                       (app.nav.tab == TAB_NETWORK && app.net_screen == NETS_OSK) ||
                       (app.nav.tab == TAB_SETTINGS && app.nick_open);
        if (osk_open) osk_set_hold_shift(&app.osk, (buttons & NB_LTRIGGER) != 0);
        build_model(&app);
        if (app.nav.depth == 0) nav_handle(&app.nav, ACT_NONE, &app.model);   // re-clamp focus after data changes
        Action acts[12];
        int n = input_actions(pressed, app.confirm_cross, acts, 12);
        for (int i = 0; i < n; i++) {
            handle_action(&app, acts[i]);
            build_model(&app);
        }
        if (home == HOME_TAP) {
            handle_action(&app, ACT_HOME);
            build_model(&app);
        } else if (home == HOME_HOLD) {
            home_hold(&app);
        }
        music_tick(&app);
        network_tick(&app);
        selfguard_tick(&app);
        {   // USB / FTP screens: screen off after a minute without a press (listening mode has its own)
            static int idle_blanked;
            // USB without a cable: no countdown, the screen waits on (author's request).
            int idle_on = network_fullscreen(&app) && !(app.net_screen == NETS_USB && !usb_cable());
            int blank = idle_update(&app.idle, app.now_ms, idle_on);
            if (blank != idle_blanked) {
                power_set_screen(!blank);
                idle_blanked = blank;
            }
        }
        {   // XMB power saving: backlight auto-off and auto sleep (not while music / FTP / USB run)
            static int ps_blanked;
            int busy = music_playing(&app) || app.ftp.phase != FTP_OFF || usb_active();
            int own = network_fullscreen(&app) || app.listening;
            PowerSaveOut ps = powersave_update(&app.psave, app.now_ms, app.backlight_s, app.sleep_s, busy, own);
            // A screen that owns the display (USB/FTP/listening) opening while we blanked: light it first.
            if (ps.screen_off != ps_blanked) power_set_screen(!ps.screen_off);
            ps_blanked = ps.screen_off;
            if (ps.sleep_now) {
                log_msg("power: auto sleep after %d s", app.sleep_s);
                music_save_state(&app);
                power_sleep();
            }
        }
        if (power_screen_is_off()) {
            SDL_Delay(20);                    // screen off: no drawing, just keep input and music going
            continue;
        }
        draw(&app);
        if (!app.media_suspended) art_tick(&app.games, &app.mlib);   // FTP/USB: caches stay empty

        frames++;
        if (app.now_ms - fps_start >= 1000) {
            // Diagnostics for the "card stuck in its opening animation / toast never hides" report.
            if (nav_top(&app.nav) == SCR_CARD || app.now_ms < app.toast_until)
                log_msg("timing: now=%u card_opened=%u card_t=%u toast_until=%u toast=%d",
                        (unsigned)app.now_ms, (unsigned)app.card_opened_ms,
                        (unsigned)(app.now_ms - app.card_opened_ms), (unsigned)app.toast_until,
                        app.now_ms < app.toast_until);
            if (frames < 50) log_msg("perf: %d fps (tab=%d depth=%d)", frames, app.nav.tab, app.nav.depth);
            app.fps = frames;
            frames = 0;
            fps_start = app.now_ms;
        }
    }
    return 0;
}
