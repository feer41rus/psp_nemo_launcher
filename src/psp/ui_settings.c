#include "ui.h"
#include "utf8.h"
#include "gfx.h"
#include "launch.h"
#include "platform.h"
#include "rtc.h"
#include "arkopts.h"
#include "bgsave.h"
#include <stdlib.h>
#include <pspiofilemgr.h>
#include "sysreg.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

#include "version.h"
#include "device.h"
#include "logrot.h"
#include "battery_hw.h"
#include "libbg.h"
#include "libbg_hw.h"

#define ROW_H 28
#define VISIBLE_ROWS 7          // author: smaller version line, one more row
#define LIST_X 24
#define LIST_W (SCREEN_W - 48 - 10)
#define BAR_X (SCREEN_W - 26)
#define DATE_DIR "/CONFIG/DATE"
#define SYS_DIR "/CONFIG/SYSTEM"
#define XMB_DIR "/CONFIG/SYSTEM/XMB"
#define PS_DIR "/CONFIG/SYSTEM/POWER_SAVING"
#define LANG_REG_RU 8
#define LANG_REG_EN 1
#define IMPOSE_LANGUAGE 0x80
#define NICK_MAX 25

void settings_load_language(void) {
    int v = LANG_REG_RU;
    sysreg_get_int(XMB_DIR, "language", &v);
    i18n_set(v == LANG_REG_RU ? LANG_RU : LANG_EN);
    log_msg("settings: system language %d", v);
}

void settings_init(App *a) {
    setmenu_init(&a->setmenu, a->has_wifi);
    a->setmenu.is_go = platform_model() == 4;   // PSP Go: internal storage rows (1.2.6)
    a->setmenu.show_site = a->lib->settings.site;
    int v;
    a->date_format = sysreg_get_int(DATE_DIR, "date_format", &v) == 0 && v >= 0 && v <= 2 ? v : DF_YMD;
    a->time_h12 = sysreg_get_int(DATE_DIR, "time_format", &v) == 0 ? v == 1 : 0;
    a->tz_minutes = sysreg_get_int(DATE_DIR, "time_zone_offset", &v) == 0 ? v : 0;
    if (sysreg_get_str(SYS_DIR, "owner_name", a->nickname, sizeof a->nickname) != 0) a->nickname[0] = '\0';
    a->backlight_s = sysreg_get_int(PS_DIR, "backlight_off_interval", &v) == 0 ? v : 0;
    a->sleep_s = sysreg_get_int(PS_DIR, "suspend_interval", &v) == 0 ? v : 0;
    powersave_reset(&a->psave, platform_ms());   // now_ms is not set yet at start
    a->ark_text = platform_ark_read_text();
    log_msg("settings: backlight_off=%d s suspend=%d s", a->backlight_s, a->sleep_s);
    log_msg("settings: date_format=%d time_h12=%d tz=%d", a->date_format, a->time_h12, a->tz_minutes);
    DateTime now;
    if (rtc_get_local(&now) == 0 && datetime_clock_lost(&now)) {
        log_msg("settings: clock lost (%04d), asking", now.year);
        a->clock_prompt = 1;
        a->clock_prompt_sel = 0;
    }
}

void settings_open_ark(App *a) {
    log_msg("settings: open ARK menu");
    music_save_state(a);
    net_shutdown();
    selfguard_clean_exit();
    int rc = launch_ark_menu();
    platform_set_launcher_mode(a->lib->settings.replace_xmb);   // still here: undo what the attempt cleared
    ui_toast(a, tr(rc == -2 ? S_ERR_SETTINGS_WRITE : S_ERR_NO_ARK_MENU));
}

void settings_exit_to_xmb(App *a) {
    music_save_state(a);
    net_shutdown();
    selfguard_clean_exit();
    if (launch_exit_to_xmb() < 0) {
        log_msg("settings: exit to XMB failed (SETTINGS.TXT not updated)");
        ui_toast(a, tr(S_ERR_SETTINGS_WRITE));
    }
}

static int zone_index(int minutes) {
    for (int i = 0; i < TIME_ZONE_COUNT; i++)
        if (TIME_ZONES[i].minutes == minutes) return i;
    return -1;
}

static void zone_text(int minutes, char *out, size_t cap) {
    int i = zone_index(minutes), m = minutes < 0 ? -minutes : minutes;
    const char *city = i < 0 ? "" : i18n_get() == LANG_EN ? TIME_ZONES[i].city_en : TIME_ZONES[i].city_ru;
    snprintf(out, cap, "UTC%c%d:%02d %s", minutes < 0 ? '-' : '+', m / 60, m % 60, city);
}

static int set_reg_in(App *a, const char *dir, const char *key, int value) {
    int rc = sysreg_set_int(dir, key, value);
    log_msg("settings: %s=%d rc=%d", key, value, rc);
    if (rc != 0) ui_fail(a, S_ST_REG_FAILED);
    return rc;
}

static int set_reg(App *a, const char *key, int value) { return set_reg_in(a, DATE_DIR, key, value); }

static int brightness_now(void) {
    int b = platform_impose_get(IMPOSE_BRIGHTNESS);   // follows the brightness button on the PSP
    return b >= 0 && b <= 3 ? b : 0;
}

static void change_ark(App *a, SetId id, int delta);
static int pick_count(SetId id);
static int pick_current(const App *a, SetId id);
static int pick_needs_ark(SetId id);
static void pick_apply(App *a, SetId id, int i);

static void change(App *a, SetId id, int delta) {
    log_dbg("settings: change %s (id %d, delta %d)", tr_en(setmenu_entry(id)->label), (int)id, delta);
    if (setmenu_entry(id)->dropdown) {        // ←/→ on a drop-down row: the next value of the same list
        if (pick_needs_ark(id) && !a->ark_text) { ui_fail(a, S_ST_ARK_READ_FAILED); return; }
        int n = pick_count(id);
        pick_apply(a, id, (pick_current(a, id) + delta + n) % n);
        return;
    }
    switch (id) {
    case SID_TIME_FORMAT:
        if (set_reg(a, "time_format", !a->time_h12) == 0) a->time_h12 = !a->time_h12;
        break;
    case SID_LANGUAGE: {                      // the system language: the XMB switches too
        int ru = i18n_get() != LANG_RU;
        if (set_reg_in(a, XMB_DIR, "language", ru ? LANG_REG_RU : LANG_REG_EN) == 0) {
            platform_impose_set(IMPOSE_LANGUAGE, ru ? LANG_REG_RU : LANG_REG_EN);
            i18n_set(ru ? LANG_RU : LANG_EN);
        }
        break;
    }
    case SID_WPA2: network_toggle_wpa2(a); break;
    case SID_USBCHARGE: case SID_NOLED: case SID_NOUMD: case SID_NOANALOG:   // on/off ARK options step
        change_ark(a, id, delta);
        break;
    case SID_REPLACE_XMB: {
        NemoSettings *s = &a->lib->settings;
        s->replace_xmb = !s->replace_xmb;
        library_save(a->lib, NEMO_DATA_PATH);
        int rc = platform_set_launcher_mode(s->replace_xmb);
        log_msg("settings: replace_xmb=%d launcher rc=%d", s->replace_xmb, rc);
        if (rc != 0) ui_toast(a, tr(S_ERR_SETTINGS_WRITE));
        else if (s->replace_xmb) ui_toast(a, tr(S_RX_ON_NOTE));
        break;
    }
    case SID_SHOW_XMB_EXIT: case SID_NCL_LOGO: case SID_SITE: case SID_SHOW_MUSIC: case SID_SHOW_PHOTO:
    case SID_SPLASH_HELLO: case SID_SPLASH_NCL: case SID_UI_SOUNDS: case SID_CONTROL_CENTER: case SID_BAT_PCT: case SID_LOW_SPACE: {
        NemoSettings *s = &a->lib->settings;
        int *f = id == SID_SHOW_XMB_EXIT ? &s->xmb_exit : id == SID_NCL_LOGO ? &s->ncl_logo : id == SID_SITE ? &s->site
                 : id == SID_BAT_PCT ? &s->bat_pct : id == SID_LOW_SPACE ? &s->low_space
                 : id == SID_SPLASH_HELLO ? &s->splash_hello : id == SID_SPLASH_NCL ? &s->splash_ncl
                 : id == SID_UI_SOUNDS ? &s->ui_sounds : id == SID_CONTROL_CENTER ? &s->control_center
                 : id == SID_SHOW_PHOTO ? &s->show_photo : &s->show_music;
        *f = !*f;
        library_save(a->lib, NEMO_DATA_PATH);
        log_msg("settings: nemo %d=%d", id, *f);
        if (id == SID_SHOW_MUSIC) music_set_enabled(a, *f);
        if (id == SID_SPLASH_NCL && *f) ui_toast(a, tr(S_ST_NCL_ARMED));
        if (id == SID_CONTROL_CENTER) {
            cc_deploy(a);
            ui_toast(a, tr(S_ST_CC_NOTE));
        }
        a->setmenu.show_site = s->site;
        break;
    }
    case SID_VERBOSE_LOG: {   // the detailed log for beta testing (1.3.3): on at once, and on from the next start
        NemoSettings *s = &a->lib->settings;
        s->verbose_log = !s->verbose_log;
        library_save(a->lib, NEMO_DATA_PATH);
        log_set_verbose(s->verbose_log);
        if (s->verbose_log) diag_passport(a);
        break;
    }
    case SID_DBG_RAM: case SID_DBG_CPU: case SID_DBG_FPS: {   // debug box rows (1.2.0)
        NemoSettings *s = &a->lib->settings;
        int *f = id == SID_DBG_RAM ? &s->dbg_ram : id == SID_DBG_CPU ? &s->dbg_cpu : &s->dbg_fps;
        *f = !*f;
        library_save(a->lib, NEMO_DATA_PATH);
        log_msg("settings: debug %d=%d", id, *f);
        if (*f) a->debug = 1;                 // show it right away; SELECT+TRIANGLE hides it
        break;
    }
    case SID_CONFIRM: {
        int next = !a->confirm_cross;
        int rc = sysreg_set_confirm_cross(next);
        log_msg("settings: set button_assign=%d rc=%d", next, rc);
        if (rc == 0) a->confirm_cross = next;
        else ui_toast(a, tr(S_ERR_REGISTRY));
        break;
    }
    default: break;
    }
}

static const ArkOpt *ark_opt(SetId id) {
    switch (id) {
    case SID_USBCHARGE: return &ARK_OPT_USBCHARGE;
    case SID_HIGHMEM: return &ARK_OPT_HIGHMEM;
    case SID_MSCACHE: return &ARK_OPT_MSCACHE;
    case SID_INFERNO: return &ARK_OPT_INFERNO;
    case SID_SKIPLOGO: return &ARK_OPT_SKIPLOGOS;
    case SID_NOLED: return &ARK_OPT_NOLED;
    case SID_NOUMD: return &ARK_OPT_NOUMD;
    case SID_NOANALOG: return &ARK_OPT_NOANALOG;
    case SID_UMDREGION: return &ARK_OPT_REGION;
    default: return NULL;
    }
}

static int clock_target(SetId id) {
    return id == SID_OC_GAMES ? CLK_GAME : id == SID_OC_XMB ? CLK_VSH : id == SID_OC_NEMO ? CLK_LAUNCHER : -1;
}

// Replace the SETTINGS.TXT cache with next (written first); a toast says when it applies.
static void ark_commit(App *a, char *next, const char *what) {
    if (!next) return;
    int rc = platform_ark_write_text(next);
    log_msg("settings: ark %s rc=%d", what, rc);
    if (rc != 0) {
        free(next);
        ui_fail(a, S_ST_REG_FAILED);
        return;
    }
    free(a->ark_text);
    a->ark_text = next;
    ui_toast(a, tr(S_ST_APPLY_RESTART));
}

static int clock_index(const App *a, SetId id) {
    int cur = a->ark_text ? arkclock_get(a->ark_text, (ClockTarget)clock_target(id)) : 0, i = 0;
    for (int k = 0; k < 8; k++) if (ARK_CLOCKS[k] == cur) i = k;
    return i;
}

static void set_clock(App *a, SetId id, int index);

static void change_ark(App *a, SetId id, int delta) {
    if (!a->ark_text) { ui_fail(a, S_ST_ARK_READ_FAILED); return; }
    const ArkOpt *o = ark_opt(id);
    if (o) {
        int c = (arkopt_get(a->ark_text, o) + delta + o->count) % o->count;
        char what[48];
        snprintf(what, sizeof what, "%s=%d", o->family, c);
        ark_commit(a, arkopt_set(a->ark_text, o, c), what);
    }
}

static void set_clock(App *a, SetId id, int index) {
    int t = clock_target(id);
    if (t < 0 || !a->ark_text) return;
    int mhz = ARK_CLOCKS[index];
    char what[48];
    snprintf(what, sizeof what, "clock[%d]=%d", t, mhz);
    ark_commit(a, arkclock_set(a->ark_text, (ClockTarget)t, mhz), what);
    if (t == CLK_LAUNCHER && mhz > 333) ui_toast(a, tr(S_ST_OC_NEMO_NOTE));
}

// Drop-down lists (SC_PICK): every setting with more than two values (author, 1.3.8); ←/→ on the row step
// through the same list.
#define PICK_ROWS 9

static int pick_count(SetId id) {
    const ArkOpt *o = ark_opt(id);
    if (o) return o->count;
    switch (id) {
    case SID_TIMEZONE: return TIME_ZONE_COUNT;
    case SID_LOG_KEEP: return LOG_KEEP_CHOICE_COUNT;
    case SID_LIB_BG: return LIBBG_COUNT;
    case SID_DATE_FORMAT: return 3;
    case SID_BRIGHTNESS: return 4;
    case SID_BACKLIGHT: return 6;
    case SID_AUTOSLEEP: return 7;
    default: return 8;                        // overclock: ARK_CLOCKS
    }
}

static int pick_current(const App *a, SetId id) {
    const ArkOpt *o = ark_opt(id);
    if (o) return a->ark_text ? arkopt_get(a->ark_text, o) : 0;
    switch (id) {
    case SID_LOG_KEEP:
        for (int i = 0; i < LOG_KEEP_CHOICE_COUNT; i++)
            if (LOG_KEEP_CHOICES[i] == a->lib->settings.log_keep) return i;
        return 0;
    case SID_TIMEZONE: {
        int i = zone_index(a->tz_minutes);
        return i < 0 ? 0 : i;
    }
    case SID_LIB_BG: return libbg_valid(a->lib->settings.lib_bg);
    case SID_DATE_FORMAT: return a->date_format;
    case SID_BRIGHTNESS: return brightness_now();
    case SID_BACKLIGHT: return powersave_choice_index(BACKLIGHT_CHOICES, 6, a->backlight_s);
    case SID_AUTOSLEEP: return powersave_choice_index(SLEEP_CHOICES, 7, a->sleep_s);
    default: return clock_index(a, id);
    }
}

static void seconds_text(int s, char *out, size_t cap) {
    if (s <= 0) snprintf(out, cap, "%s", tr(S_OFF));
    else if (s < 60) snprintf(out, cap, tr(S_ST_SEC_FMT), s);
    else snprintf(out, cap, tr(S_ST_MIN_FMT), s / 60);
}

static void pick_label(SetId id, int i, char *out, size_t cap) {
    static const StrId DF[3] = { S_ST_DF_YMD, S_ST_DF_MDY, S_ST_DF_DMY };
    static const StrId HIGHMEM[5] = { S_ST_V_OFF, S_ST_V_DEFAULT, S_ST_V_AUTO, S_ST_V_16MB, 0 };
    static const StrId SKIP[4] = { S_ST_V_OFF, S_ST_V_ON, S_ST_V_GAMEBOOT, S_ST_V_COLDBOOT };
    static const StrId REGION[4] = { S_ST_V_DEFAULT, S_ST_V_US, S_ST_V_EU, S_ST_V_JP };
    static const char *const MSC[4] = { NULL, "4 KiB", "8 KiB", "16 KiB" };
    static const char *const INF[3] = { NULL, "LRU", "RR" };
    switch (id) {
    case SID_TIMEZONE: zone_text(TIME_ZONES[i].minutes, out, cap); return;
    case SID_LOG_KEEP:
        if (!LOG_KEEP_CHOICES[i]) snprintf(out, cap, "%s", tr(S_ST_V_OFF));
        else snprintf(out, cap, "%d", LOG_KEEP_CHOICES[i]);
        return;
    case SID_LIB_BG: snprintf(out, cap, "%s", tr(libbg_name(i))); return;
    case SID_DATE_FORMAT: snprintf(out, cap, "%s", tr(DF[i])); return;
    case SID_BRIGHTNESS: snprintf(out, cap, tr(S_ST_LEVEL_FMT), i + 1); return;
    case SID_BACKLIGHT: seconds_text(BACKLIGHT_CHOICES[i], out, cap); return;
    case SID_AUTOSLEEP: seconds_text(SLEEP_CHOICES[i], out, cap); return;
    case SID_HIGHMEM: snprintf(out, cap, "%s", i == 4 ? "MAX" : tr(HIGHMEM[i])); return;
    case SID_SKIPLOGO: snprintf(out, cap, "%s", tr(SKIP[i])); return;
    case SID_UMDREGION: snprintf(out, cap, "%s", tr(REGION[i])); return;
    case SID_MSCACHE: snprintf(out, cap, "%s", i ? MSC[i] : tr(S_ST_V_OFF)); return;
    case SID_INFERNO: snprintf(out, cap, "%s", i ? INF[i] : tr(S_ST_V_OFF)); return;
    case SID_USBCHARGE: case SID_NOLED: case SID_NOUMD: case SID_NOANALOG:
        snprintf(out, cap, "%s", tr(i ? S_ST_V_ON : S_ST_V_OFF));
        return;
    default:
        if (!ARK_CLOCKS[i]) snprintf(out, cap, "%s", tr(S_ST_V_AUTO));
        else snprintf(out, cap, tr(ARK_CLOCKS[i] > 333 ? S_ST_MHZ_EXP_FMT : S_ST_MHZ_FMT), ARK_CLOCKS[i]);
        return;
    }
}

// The list needs ARK's settings file for ARK options and clocks.
static int pick_needs_ark(SetId id) { return ark_opt(id) || clock_target(id) >= 0; }

static void pick_apply(App *a, SetId id, int i) {
    if (i == pick_current(a, id)) return;
    const ArkOpt *o = ark_opt(id);
    if (o) {
        if (!a->ark_text) return;
        char what[48];
        snprintf(what, sizeof what, "%s=%d", o->family, i);
        ark_commit(a, arkopt_set(a->ark_text, o, i), what);
        return;
    }
    switch (id) {
    case SID_LOG_KEEP:                        // the new count applies from the next start; Off stops now
        a->lib->settings.log_keep = LOG_KEEP_CHOICES[i];
        library_save(a->lib, NEMO_DATA_PATH);
        log_msg("settings: log_keep=%d", LOG_KEEP_CHOICES[i]);
        if (!LOG_KEEP_CHOICES[i]) log_disable();
        return;
    case SID_TIMEZONE:
        if (set_reg(a, "time_zone_offset", TIME_ZONES[i].minutes) == 0) a->tz_minutes = TIME_ZONES[i].minutes;
        return;
    case SID_LIB_BG:                          // 1.3.8: "Your own" only once the Gallery has made one
        if (i == LIBBG_OWN && !libbg_exists()) { ui_toast(a, tr(S_ST_LIB_BG_NONE)); return; }
        a->lib->settings.lib_bg = i;
        library_save(a->lib, NEMO_DATA_PATH);
        log_msg("settings: lib_bg=%d", i);
        return;
    case SID_DATE_FORMAT:
        if (set_reg(a, "date_format", i) == 0) a->date_format = i;
        return;
    case SID_BRIGHTNESS:
        platform_impose_set(IMPOSE_BRIGHTNESS, i);
        set_reg_in(a, SYS_DIR, "backlight_brightness", i);
        return;
    case SID_BACKLIGHT:
        if (set_reg_in(a, PS_DIR, "backlight_off_interval", BACKLIGHT_CHOICES[i]) == 0) {
            a->backlight_s = BACKLIGHT_CHOICES[i];
            powersave_reset(&a->psave, a->now_ms);
        }
        return;
    case SID_AUTOSLEEP:
        if (set_reg_in(a, PS_DIR, "suspend_interval", SLEEP_CHOICES[i]) == 0) {
            a->sleep_s = SLEEP_CHOICES[i];
            powersave_reset(&a->psave, a->now_ms);
        }
        return;
    default: set_clock(a, id, i); return;
    }
}

static void pick_scroll(App *a) {
    if (a->pick_sel < a->pick_top) a->pick_top = a->pick_sel;
    if (a->pick_sel >= a->pick_top + PICK_ROWS) a->pick_top = a->pick_sel - PICK_ROWS + 1;
}

extern const unsigned char testers_txt[], credits_ru[], credits_en[], nemo_logo[], changelog_ru[], changelog_en[];
extern const unsigned int testers_txt_size, credits_ru_size, credits_en_size, nemo_logo_size, changelog_ru_size,
    changelog_en_size;

// Embedded text (bin2c, not NUL-terminated) as a text page.
static void open_embedded(App *a, StrId title, const unsigned char *data, unsigned size) {
    char *t = malloc(size + 1);
    if (!t) return;
    memcpy(t, data, size);
    t[size] = '\0';
    textpage_open(a, title, t);
    free(t);
}

static void open_date_editor(App *a) {
    if (rtc_get_local(&a->dt_edit) != 0 || datetime_clock_lost(&a->dt_edit)) {
        DateTime d = { 2026, 1, 1, 12, 0 };   // a flat battery left 1970: start from a sensible date
        a->dt_edit = d;
    }
    if (a->dt_edit.year > DT_YEAR_MAX) a->dt_edit.year = DT_YEAR_MAX;
    a->dt_field = 0;
    a->dt_open = 1;
}

static void activate(App *a, SetId id) {
    switch (id) {
    case SID_DATETIME: open_date_editor(a); break;
    case SID_PLUGINS: plugins_open(a); break;
    case SID_SAVES: saves_open(a, NULL, tr(S_ST_SAVES)); break;
    case SID_BAT_DETAILS: battery_open(a, BATS_DETAILS); break;
    case SID_BAT_TEST: battery_open(a, BATS_TEST); break;
    case SID_MEM_USAGE: memory_open(a, MEMS_USAGE); break;
    case SID_MEM_DETAILS: memory_open(a, MEMS_DETAILS); break;
    case SID_CREATORS: textpage_open(a, S_ST_CREATORS, tr(S_AB_CREATORS_TEXT)); break;
    case SID_TESTERS: open_embedded(a, S_ST_TESTERS, testers_txt, testers_txt_size); break;
    case SID_NEMO_VERSION:                    // assets/changelog_ru.txt / changelog_en.txt (1.2.0)
        if (i18n_get() == LANG_EN) open_embedded(a, S_ST_CHANGES, changelog_en, changelog_en_size);
        else open_embedded(a, S_ST_CHANGES, changelog_ru, changelog_ru_size);
        break;
    case SID_THANKS:                          // docs/CREDITS.md / CREDITS.en.md by the system language
        if (i18n_get() == LANG_EN) open_embedded(a, S_ST_THANKS, credits_en, credits_en_size);
        else open_embedded(a, S_ST_THANKS, credits_ru, credits_ru_size);
        break;
    case SID_RESET:
        memset(a->reset_marks, 0, sizeof a->reset_marks);
        a->reset_sel = 0;
        a->reset_confirm = 0;
        a->reset_open = 1;
        break;
    case SID_NICKNAME:
        a->osk_purpose = OSK_FOR_NICKNAME;
        osk_init(&a->osk, a->nickname);
        osk_set_length_rule(&a->osk, 1, NICK_MAX);
        a->nick_open = 1;
        break;
    case SID_SLEEP: power_do(a, PI_SLEEP); break;
    case SID_RESTART: power_do(a, PI_REBOOT); break;
    case SID_SHUTDOWN: power_do(a, PI_SHUTDOWN); break;
    case SID_TO_ARK: settings_open_ark(a); break;
    case SID_TO_XMB: settings_exit_to_xmb(a); break;
    default: break;
    }
}

static void copy_file(const char *from, const char *to) {
    SceUID in = sceIoOpen(from, PSP_O_RDONLY, 0);
    if (in < 0) return;
    SceUID out = sceIoOpen(to, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    static char buf[16 * 1024];
    int n;
    while (out >= 0 && (n = sceIoRead(in, buf, sizeof buf)) > 0) sceIoWrite(out, buf, n);
    if (out >= 0) sceIoClose(out);
    sceIoClose(in);
}

static void remove_dir_files(const char *dir) {
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    memset(&e, 0, sizeof e);
    char path[512];
    while (sceIoDread(d, &e) > 0) {
        if (e.d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, e.d_name);
        sceIoRemove(path);
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
}

// Settings › System › Reset: only what is marked; then Nemo restarts with the clean state.
static void do_reset(App *a) {
    const int *mk = a->reset_marks;
    log_msg("reset: console=%d ark=%d nemo=%d data=%d", mk[0], mk[1], mk[2], mk[3]);
    music_save_state(a);
    if (mk[0]) {                              // only what Nemo itself changes; language and zone stay
        sysreg_set_int(DATE_DIR, "date_format", DF_DMY);
        sysreg_set_int(DATE_DIR, "time_format", 0);
        sysreg_set_int(SYS_DIR, "backlight_brightness", 3);
        platform_impose_set(IMPOSE_BRIGHTNESS, 3);
        sysreg_set_int(PS_DIR, "backlight_off_interval", 0);
        sysreg_set_int(PS_DIR, "suspend_interval", 0);
        sysreg_set_confirm_cross(1);
    }
    if (mk[1]) {
        char *cur = platform_ark_read_text();
        char *next = arkopts_factory_reset(cur ? cur : "");
        int rc = next ? platform_ark_write_text(next) : -1;
        log_msg("reset: SETTINGS.TXT factory rc=%d", rc);
        free(cur);
        free(next);
    }
    NemoSettings keep = a->lib->settings;
    if (mk[2]) library_settings_defaults(&keep);
    if (mk[3]) {                              // a copy first: a mistaken reset can be undone by hand
        copy_file(NEMO_DATA_PATH, NEMO_DATA_PATH ".bak");
        copy_file("ms0:/PSP/NEMO/music.json", "ms0:/PSP/NEMO/music.json.bak");
        bgsave_cancel();
        sceIoRemove("ms0:/PSP/NEMO/music.json");
        sceIoRemove("ms0:/PSP/NEMO/scan.json");
        sceIoRemove("ms0:/PSP/NEMO/music_scan.json");
        remove_dir_files("ms0:/PSP/NEMO/cache");
        library_init(a->lib);
    }
    a->lib->settings = keep;
    library_save(a->lib, NEMO_DATA_PATH);
    log_msg("reset: done, restarting Nemo");
    selfguard_clean_exit();
    ui_toast(a, tr(S_RS_DONE));
    net_shutdown();
    launch_self();
}

static void reset_action(App *a, Action act) {
    if (a->reset_confirm) {                   // "Reset the selected items?" — No first
        if (act == ACT_LEFT || act == ACT_RIGHT) a->reset_confirm = a->reset_confirm == 1 ? 2 : 1;
        else if (act == ACT_BACK) a->reset_confirm = 0;
        else if (act == ACT_CONFIRM) {
            if (a->reset_confirm == 1) do_reset(a);
            a->reset_confirm = 0;
        }
        return;
    }
    switch (act) {
    case ACT_UP: if (a->reset_sel > 0) a->reset_sel--; break;
    case ACT_DOWN: if (a->reset_sel < 4) a->reset_sel++; break;
    case ACT_BACK: a->reset_open = 0; break;
    case ACT_CONFIRM:
        if (a->reset_sel < 4) a->reset_marks[a->reset_sel] = !a->reset_marks[a->reset_sel];
        else if (a->reset_marks[0] | a->reset_marks[1] | a->reset_marks[2] | a->reset_marks[3]) a->reset_confirm = 2;
        else ui_toast(a, tr(S_RS_NOTHING));
        break;
    default: break;
    }
}

static void editor_action(App *a, Action act) {
    switch (act) {
    case ACT_LEFT: if (a->dt_field > 0) a->dt_field--; break;
    case ACT_RIGHT: if (a->dt_field < 4) a->dt_field++; break;
    case ACT_UP: case ACT_DOWN: {
        int order[5];
        datetime_field_order((DateFormat)a->date_format, order);   // dt_field is the position on screen
        datetime_step(&a->dt_edit, order[a->dt_field], act == ACT_UP ? 1 : -1);
        break;
    }
    case ACT_BACK: a->dt_open = 0; break;
    case ACT_CONFIRM: case ACT_START: {
        int rc = rtc_set_local(&a->dt_edit, a->tz_minutes);
        ui_toast(a, tr(rc == 0 ? S_ST_DT_SAVED : S_ST_DT_FAILED));
        if (rc == 0) a->dt_open = 0;
        break;
    }
    default: break;
    }
}

static void nick_action(App *a, Action act) {
    OskState st = osk_handle(&a->osk, act);
    if (st == OSK_CANCEL) { a->nick_open = 0; return; }
    if (st != OSK_DONE) return;
    int rc = sysreg_set_str(SYS_DIR, "owner_name", a->osk.text);
    log_msg("settings: owner_name rc=%d", rc);
    if (rc == 0) {
        utf8_copy(a->nickname, sizeof a->nickname, a->osk.text);   // a name keeps to OSK_NAME_MAX
        ui_toast(a, tr(S_ST_NICK_SAVED));
    } else {
        ui_fail(a, S_ST_REG_FAILED);
    }
    a->nick_open = 0;
}

void settings_action(App *a, Action act) {
    if (a->oc_warn) {                         // "overclocking is at your own risk": ✕ stays, ○ leaves
        if (act == ACT_CONFIRM) a->oc_warn = 0;
        else if (act == ACT_BACK) { a->oc_warn = 0; setmenu_handle(&a->setmenu, ACT_BACK, VISIBLE_ROWS); }
        return;
    }
    if (a->pick_open) {                       // drop-down list
        int n = pick_count(a->pick_id);
        if (act == ACT_UP && a->pick_sel > 0) a->pick_sel--;
        else if (act == ACT_DOWN && a->pick_sel < n - 1) a->pick_sel++;
        else if (act == ACT_LEFT) a->pick_sel = a->pick_sel > PICK_ROWS ? a->pick_sel - PICK_ROWS : 0;
        else if (act == ACT_RIGHT) a->pick_sel = a->pick_sel + PICK_ROWS < n ? a->pick_sel + PICK_ROWS : n - 1;
        else if (act == ACT_BACK) a->pick_open = 0;
        else if (act == ACT_CONFIRM) {
            a->pick_open = 0;
            pick_apply(a, a->pick_id, a->pick_sel);
        }
        pick_scroll(a);
        return;
    }
    if (a->plug) { plugins_action(a, act); return; }
    if (a->reset_open) { reset_action(a, act); return; }
    if (a->nick_open) { nick_action(a, act); return; }
    if (a->dt_open) { editor_action(a, act); return; }
    SetCmd c = setmenu_handle(&a->setmenu, act, VISIBLE_ROWS);
    switch (c.type) {
    case SC_SOON: {
        char msg[96];
        snprintf(msg, sizeof msg, tr(S_ST_SOON_FMT), setmenu_entry(c.id)->soon);
        ui_toast(a, msg);
        break;
    }
    case SC_CHANGE: change(a, c.id, c.delta); break;
    case SC_PICK:
        if (pick_needs_ark(c.id) && !a->ark_text) { ui_fail(a, S_ST_ARK_READ_FAILED); break; }
        a->pick_id = c.id;
        a->pick_sel = pick_current(a, c.id);
        a->pick_top = 0;
        pick_scroll(a);
        a->pick_open = 1;
        break;
    case SC_OPEN:
        if (c.id == SID_OVERCLOCK) a->oc_warn = 1;
        if (c.id == SID_ARK || c.id == SID_OVERCLOCK) {   // the file may have changed (ARK menu, PC)
            free(a->ark_text);
            a->ark_text = platform_ark_read_text();
        }
        break;
    case SC_ACTIVATE: activate(a, c.id); break;
    default: break;
    }
}

// "The PSP clock was reset. Set it now?" — once after start when the year is before 2020.
void settings_clock_prompt_action(App *a, Action act) {
    if (act == ACT_LEFT || act == ACT_RIGHT) a->clock_prompt_sel = !a->clock_prompt_sel;
    else if (act == ACT_BACK) a->clock_prompt = 0;
    else if (act == ACT_CONFIRM) {
        a->clock_prompt = 0;
        if (a->clock_prompt_sel == 0) {
            a->nav.tab = TAB_SETTINGS;
            a->nav.depth = 0;
            setmenu_init(&a->setmenu, a->has_wifi);
            a->setmenu.is_go = platform_model() == 4;   // PSP Go: internal storage rows (1.2.6)
            a->setmenu.show_site = a->lib->settings.site;   // init now clears it (1.3.12)
            setmenu_handle(&a->setmenu, ACT_CONFIRM, VISIBLE_ROWS);   // root row 0 = Time
            open_date_editor(a);
        }
    }
}

static void value_text(const App *a, const SetEntry *e, char *out, size_t cap) {
    out[0] = '\0';
    // Drop-down rows show the label from their list; the rows below keep their exact value even when it is
    // not one of the list's (a time zone, a clock or a timeout set elsewhere).
    int exact = e->id == SID_TIMEZONE || e->id == SID_BACKLIGHT || e->id == SID_AUTOSLEEP || clock_target(e->id) >= 0;
    if (e->dropdown && !exact) {
        if (pick_needs_ark(e->id) && !a->ark_text) return;
        pick_label(e->id, pick_current(a, e->id), out, cap);
        return;
    }
    switch (e->id) {
    case SID_TIME_FORMAT: snprintf(out, cap, "%s", tr(a->time_h12 ? S_ST_TF_12 : S_ST_TF_24)); break;
    case SID_TIMEZONE: zone_text(a->tz_minutes, out, cap); break;
    case SID_LANGUAGE: snprintf(out, cap, "%s", tr(S_LANG_NAME)); break;
    case SID_WPA2: snprintf(out, cap, "%s", tr(a->wpa2_on ? S_ON : S_OFF)); break;
    case SID_NICKNAME: snprintf(out, cap, "%s", a->nickname); break;
    case SID_GITHUB: snprintf(out, cap, "feer41rus/psp_nemo_launcher"); break;
    case SID_SITE_INFO: snprintf(out, cap, "nemoleague.ru"); break;
    case SID_VERBOSE_LOG: snprintf(out, cap, "%s", tr(a->lib->settings.verbose_log ? S_ST_V_ON : S_ST_V_OFF)); break;
    case SID_DBG_RAM: case SID_DBG_CPU: case SID_DBG_FPS: {
        const NemoSettings *s = &a->lib->settings;
        int on = e->id == SID_DBG_RAM ? s->dbg_ram : e->id == SID_DBG_CPU ? s->dbg_cpu : s->dbg_fps;
        snprintf(out, cap, "%s", tr(on ? S_ST_V_ON : S_ST_V_OFF));
        break;
    }
    case SID_BAT_CHARGE: case SID_BAT_STATE: {   // read at most once a second (1.2.2)
        static BatInfo b;
        static uint32_t at;
        uint32_t now = platform_ms();
        if (!at || now - at >= 1000) { battery_hw_read(&b, 0); at = now ? now : 1; }
        if (e->id == SID_BAT_CHARGE) {
            if (b.pct >= 0) snprintf(out, cap, "%d %%", b.pct);
            else snprintf(out, cap, "—");
        } else {
            StrId st = !b.exists ? S_BAT_NONE : b.charging ? (b.pct >= 100 ? S_BAT_CHARGED : S_BAT_CHARGING) : S_BAT_ON_BATTERY;
            snprintf(out, cap, "%s", tr(st));
        }
        break;
    }
    case SID_SHOW_XMB_EXIT: case SID_NCL_LOGO: case SID_SITE: case SID_SHOW_MUSIC: case SID_SHOW_PHOTO: case SID_REPLACE_XMB:
    case SID_SPLASH_HELLO: case SID_SPLASH_NCL: case SID_UI_SOUNDS: case SID_CONTROL_CENTER: case SID_BAT_PCT: case SID_LOW_SPACE: {
        const NemoSettings *s = &a->lib->settings;
        int on = e->id == SID_SHOW_XMB_EXIT ? s->xmb_exit : e->id == SID_NCL_LOGO ? s->ncl_logo
                 : e->id == SID_BAT_PCT ? s->bat_pct : e->id == SID_LOW_SPACE ? s->low_space
                 : e->id == SID_SITE ? s->site : e->id == SID_REPLACE_XMB ? s->replace_xmb
                 : e->id == SID_SPLASH_HELLO ? s->splash_hello : e->id == SID_SPLASH_NCL ? s->splash_ncl
                 : e->id == SID_UI_SOUNDS ? s->ui_sounds : e->id == SID_CONTROL_CENTER ? s->control_center
                 : e->id == SID_SHOW_PHOTO ? s->show_photo : s->show_music;
        snprintf(out, cap, "%s", tr(!on ? S_ST_V_OFF : e->id == SID_SPLASH_NCL ? S_ST_V_NEXT_START : S_ST_V_ON));
        break;
    }
    case SID_USBCHARGE: case SID_NOLED: case SID_NOUMD: case SID_NOANALOG:
        if (a->ark_text) pick_label(e->id, arkopt_get(a->ark_text, ark_opt(e->id)), out, cap);
        break;
    case SID_OC_GAMES: case SID_OC_XMB: case SID_OC_NEMO: {
        if (!a->ark_text) break;
        int mhz = arkclock_get(a->ark_text, (ClockTarget)clock_target(e->id));
        if (!mhz) snprintf(out, cap, "%s", tr(S_ST_V_AUTO));
        else snprintf(out, cap, tr(mhz > 333 ? S_ST_MHZ_EXP_FMT : S_ST_MHZ_FMT), mhz);
        break;
    }
    case SID_BACKLIGHT: seconds_text(a->backlight_s, out, cap); break;
    case SID_AUTOSLEEP: seconds_text(a->sleep_s, out, cap); break;
    case SID_DATETIME: {
        DateTime now;
        char d[16], t[16];
        if (rtc_get_local(&now) != 0) break;
        datetime_format_date(&now, (DateFormat)a->date_format, d, sizeof d);
        datetime_format_time(&now, a->time_h12, t, sizeof t);
        snprintf(out, cap, "%s  %s", d, t);
        break;
    }
    case SID_NEMO_VERSION:
        snprintf(out, cap, "%s", NEMO_VERSION);   // the stage name lives in "Changes" (1.2.0)
        break;
    case SID_MEM_FREE: memory_free_text("ms0:", out, cap); break;
    case SID_MEM_FREE_INT: memory_free_text("ef0:", out, cap); break;   // PSP Go only
    case SID_MODEL: {
        char gen[8];
        device_gen_string(platform_model(), gen, sizeof gen);
        snprintf(out, cap, "%s (%s)", device_model_name(platform_model()), gen);
        break;
    }
    default:
        if (e->kind == SK_SOON) snprintf(out, cap, "%s", e->soon);
        else if (e->kind == SK_SUBMENU) snprintf(out, cap, "›");
        break;
    }
}

void settings_ark_value_text(const App *a, SetId id, char *out, size_t cap) {
    out[0] = '\0';
    value_text(a, setmenu_entry(id), out, cap);
}

static void draw_row(const App *a, const SetEntry *e, int y, int sel) {
    int soon = e->kind == SK_SOON;
    gfx_round_rect(LIST_X, y, LIST_W, ROW_H - 4, 7, sel ? 0x24324aff : COL_PANEL);
    if (sel) gfx_round_rect_outline(LIST_X, y, LIST_W, ROW_H - 4, 7, COL_ACCENT);
    uint32_t col = soon ? 0x5b6779ff : COL_TEXT;
    if (e->id == SID_CONFIRM) {   // the current confirm button as a glyph
        gfx_text(FONT_BODY, LIST_X + 14, y + 5, col, tr(e->label));
        gfx_button_glyph(a->confirm_cross ? GLYPH_CROSS : GLYPH_CIRCLE, LIST_X + LIST_W - 22, y + 12, 16);
        return;
    }
    char value[96];
    value_text(a, e, value, sizeof value);
    int vw = value[0] ? gfx_text_width(FONT_BODY, value) : 0;
    if (vw > LIST_W / 2) vw = LIST_W / 2;
    gfx_text_fit(FONT_BODY, LIST_X + 14, y + 5, LIST_W - 40 - vw, col, tr(e->label));
    if (value[0])
        gfx_text_fit(FONT_BODY, LIST_X + LIST_W - 14 - vw, y + 5, vw, soon ? 0x5b6779ff : sel ? COL_ACCENT : COL_MUTED,
                     value);
}

static void draw_editor(const App *a) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000b0);
    int w = 400, h = 120, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(x, y, w, h, 12, 0xffffff20);
    gfx_text(FONT_BOLD, x + 16, y + 10, COL_TEXT, tr(S_ST_DT_TITLE));
    const DateTime *d = &a->dt_edit;
    int fields[5] = { d->year, d->month, d->day, d->hour, d->minute }, order[5], vals[5], widths[5];
    datetime_field_order((DateFormat)a->date_format, order);
    for (int i = 0; i < 5; i++) {
        vals[i] = fields[order[i]];
        widths[i] = order[i] == 0 ? 70 : 46;      // the year is four digits
    }
    static const char *const seps[5] = { "/", "/", "", ":", "" };
    int total = 0;
    for (int i = 0; i < 5; i++) total += widths[i] + 6 + (seps[i][0] ? 14 : i == 2 ? 22 : 0);
    int fx = x + (w - (total - 6)) / 2, fy = y + 50;
    for (int i = 0; i < 5; i++) {
        char s[8];
        snprintf(s, sizeof s, order[i] == 0 ? "%04d" : "%02d", vals[i]);
        int sel = i == a->dt_field;
        gfx_round_rect(fx, fy, widths[i], 34, 7, sel ? 0x2b5fa8ff : COL_PANEL);
        gfx_text(FONT_TITLE, fx + (widths[i] - gfx_text_width(FONT_TITLE, s)) / 2, fy + 4, COL_TEXT, s);
        fx += widths[i] + 6;
        if (seps[i][0]) {
            gfx_text(FONT_TITLE, fx, fy + 4, COL_MUTED, seps[i]);
            fx += 14;
        } else if (i == 2) {
            fx += 22;                             // gap between the date and the time
        }
    }
}

static void draw_clock_prompt(const App *a) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000a0);
    int w = 320, h = 100, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_text_fit(FONT_BODY, x + 16, y + 14, w - 32, COL_TEXT, tr(S_ST_CLOCK_LOST));
    const StrId labels[2] = { S_YES, S_NO };
    for (int i = 0; i < 2; i++) {
        int bx = x + w / 2 - 110 + i * 116, by = y + h - 36;
        gfx_round_rect(bx, by, 104, 24, 12, i == a->clock_prompt_sel ? 0x2b5fa8ff : COL_PANEL);
        const char *t = tr(labels[i]);
        gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
    }
}

void ui_settings_draw(const App *app) {
    if (app->plug) { plugins_draw(app); return; }
    const SetMenu *m = &app->setmenu;
    const SetLevel *l = &m->stack[m->depth];
    int y = CONTENT_Y + 12;
    if (l->node == SID_ABOUT_NEMO) {             // the Nemo logo instead of the section name (1.1.9)
        static SDL_Texture *logo;
        static int lw, lh, tried;
        if (!tried) {
            tried = 1;
            logo = gfx_texture_from_png(nemo_logo, nemo_logo_size);
            if (logo) SDL_QueryTexture(logo, NULL, NULL, &lw, &lh);
        }
        if (logo) gfx_texture(logo, LIST_X + 4, CONTENT_Y + 6, lw, lh, 255);
        y = CONTENT_Y + 58;
    } else if (m->depth > 0) {                    // submenu: its own name above the list
        gfx_text(FONT_BOLD, LIST_X + 4, CONTENT_Y + 6, COL_TEXT, tr(setmenu_entry(l->node)->label));
        y = CONTENT_Y + 28;
    }
    const SetEntry *e[SETMENU_MAX_ITEMS];
    int n = setmenu_items(m, l->node, e, SETMENU_MAX_ITEMS);
    int list_y = y;
    for (int i = l->top; i < n && i < l->top + VISIBLE_ROWS; i++, y += ROW_H) draw_row(app, e[i], y, i == l->sel);
    int ty, th;
    setmenu_scrollbar(n, l->top, VISIBLE_ROWS, VISIBLE_ROWS * ROW_H - 4, &ty, &th);
    if (th) {
        gfx_round_rect(BAR_X, list_y, 4, VISIBLE_ROWS * ROW_H - 4, 2, 0x2a3442ff);
        gfx_round_rect(BAR_X, list_y + ty, 4, th, 2, COL_ACCENT);
    }
    if (m->depth == 0) {                          // root: a small version line (model is in "About PSP")
        char ver[96];
        snprintf(ver, sizeof ver, "Nemo Launcher %s · %s %s", NEMO_VERSION, __DATE__, __TIME__);
        gfx_text(FONT_TINY, 26, SCREEN_H - HINT_BAR_H - 14, 0x5b6779ff, ver);
    }
    if (app->dt_open) draw_editor(app);
    if (app->nick_open) ui_osk_draw(app);
    if (app->pick_open) {                         // drop-down next to the selected row
        int pn = pick_count(app->pick_id), rows = pn < PICK_ROWS ? pn : PICK_ROWS;
        int w = app->pick_id == SID_TIMEZONE ? 300 : app->pick_id == SID_LIB_BG ? 230 : 190, rh = 22, h = rows * rh + 12;
        int x = SCREEN_W - w - 30, py = CONTENT_Y + (CONTENT_H - h) / 2;
        gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000070);
        gfx_round_rect(x, py, w, h, 10, 0x1a2230fa);
        gfx_round_rect_outline(x, py, w, h, 10, COL_ACCENT);
        int cur = pick_current(app, app->pick_id);
        for (int r = 0; r < rows; r++) {
            int i = app->pick_top + r, ry = py + 6 + r * rh;
            if (i == app->pick_sel) gfx_round_rect(x + 6, ry, w - 12, rh - 2, 6, 0x2b5fa8ff);
            char s[96];
            pick_label(app->pick_id, i, s, sizeof s);
            gfx_text_fit(FONT_BODY, x + 16, ry + 2, w - 32, i == cur ? COL_ACCENT : COL_TEXT, s);
        }
        if (pn > rows) {                          // scroll bar
            int bh = h - 12, kh = bh * rows / pn, ky = py + 6 + (bh - kh) * app->pick_top / (pn - rows);
            gfx_round_rect(x + w - 7, py + 6, 3, bh, 1, 0x2a3446ff);
            gfx_round_rect(x + w - 7, ky, 3, kh, 1, COL_ACCENT);
        }
    }
}

static void draw_oc_warning(void) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000c0);
    int w = 380, h = 130, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(x, y, w, h, 12, COL_WARN);
    gfx_text(FONT_BOLD, x + 16, y + 10, COL_WARN, tr(S_ST_OC_TITLE));
    // Wrap the warning on spaces into up to four lines.
    const char *p = tr(S_ST_OC_TEXT);
    int ly = y + 34;
    for (int line = 0; line < 4 && *p; line++, ly += 18) {
        char buf[160];
        size_t best = 0, n = 0;
        while (p[n]) {
            size_t next = n;
            while (p[next] && p[next] != ' ') next++;
            if (next >= sizeof buf) break;
            memcpy(buf, p, next);
            buf[next] = '\0';
            if (gfx_text_width(FONT_SMALL, buf) > w - 32 && best) break;
            best = next;
            if (!p[next]) break;
            n = next + 1;
        }
        if (!best) best = strlen(p) < sizeof buf - 1 ? strlen(p) : sizeof buf - 1;
        memcpy(buf, p, best);
        buf[best] = '\0';
        gfx_text(FONT_SMALL, x + 16, ly, COL_TEXT, buf);
        p += best;
        while (*p == ' ') p++;
    }
}

static void draw_reset(const App *a) {
    gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x000000c0);
    int w = 400, rh = 26, h = 40 + 5 * rh + 10, x = (SCREEN_W - w) / 2, y = CONTENT_Y + (CONTENT_H - h) / 2;
    gfx_round_rect(x, y, w, h, 12, 0x1a2230ff);
    gfx_round_rect_outline(x, y, w, h, 12, 0xffffff20);
    gfx_text(FONT_BOLD, x + 16, y + 10, COL_TEXT, tr(S_RS_TITLE));
    static const StrId L[4] = { S_RS_CONSOLE, S_RS_ARK, S_RS_NEMO, S_RS_DATA };
    for (int i = 0; i < 5; i++) {
        int ry = y + 36 + i * rh;
        if (i == a->reset_sel) gfx_round_rect(x + 8, ry, w - 16, rh - 3, 6, 0x2b5fa8ff);
        if (i < 4) {
            gfx_round_rect_outline(x + 18, ry + 5, 13, 13, 3, COL_TEXT);
            if (a->reset_marks[i]) gfx_round_rect(x + 21, ry + 8, 7, 7, 2, COL_ACCENT);
            gfx_text_fit(FONT_BODY, x + 40, ry + 3, w - 60, COL_TEXT, tr(L[i]));
        } else {
            int any = a->reset_marks[0] | a->reset_marks[1] | a->reset_marks[2] | a->reset_marks[3];
            const char *t = tr(S_RS_DO);
            gfx_text(FONT_BOLD, x + (w - gfx_text_width(FONT_BOLD, t)) / 2, ry + 3, any ? 0xff8a8aff : 0x5b6779ff, t);
        }
    }
    if (a->reset_confirm) {
        int cw = 340, chh = 100, cx = (SCREEN_W - cw) / 2, cy = CONTENT_Y + (CONTENT_H - chh) / 2;
        gfx_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, 0x00000080);
        gfx_round_rect(cx, cy, cw, chh, 12, 0x1a2230ff);
        gfx_round_rect_outline(cx, cy, cw, chh, 12, 0xd9534fff);
        gfx_text_fit(FONT_BODY, cx + 16, cy + 14, cw - 32, COL_TEXT, tr(S_RS_CONFIRM));
        const StrId B[2] = { S_YES, S_NO };
        for (int i = 0; i < 2; i++) {
            int bx = cx + cw / 2 - 110 + i * 116, by = cy + chh - 36;
            int sel = a->reset_confirm == i + 1;
            gfx_round_rect(bx, by, 104, 24, 12, sel ? (i == 0 ? 0xd9534fff : 0x2b5fa8ff) : COL_PANEL);
            const char *t = tr(B[i]);
            gfx_text(FONT_BOLD, bx + (104 - gfx_text_width(FONT_BOLD, t)) / 2, by + 4, COL_TEXT, t);
        }
    }
}

void settings_draw_overlays(const App *a) {
    if (a->reset_open && a->nav.tab == TAB_SETTINGS) draw_reset(a);
    if (a->clock_prompt) draw_clock_prompt(a);
    if (a->oc_warn && a->nav.tab == TAB_SETTINGS) draw_oc_warning();
}

void settings_draw_hints(const App *a) {
    if (a->clock_prompt) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (a->plug) {
        plugins_draw_hints(a);
    } else if (a->reset_open) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_MARK }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (a->pick_open) {
        static const Hint h[] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (a->oc_warn) {
        static const Hint h[] = { { ACT_CONFIRM, S_ST_OC_OK }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, 2);
    } else if (a->nick_open) {
        static const Hint h[] = { { ACT_CONFIRM, S_OSK_HINT_TYPE }, { ACT_QUICK, S_OSK_HINT_DEL },
                                  { ACT_TAB_PREV, S_OSK_HINT_CAPS }, { ACT_BACK, S_OSK_HINT_CANCEL },
                                  { ACT_START, S_OSK_HINT_DONE } };
        ui_hints_draw(a, h, 5);
    } else if (a->dt_open) {
        static const Hint h[] = { { ACT_CONFIRM, S_ST_DT_SAVE }, { ACT_BACK, S_HINT_BACK }, { ACT_UP, S_ST_DT_CHANGE } };
        ui_hints_draw(a, h, 3);
    } else {
        Hint h[2] = { { ACT_CONFIRM, S_HINT_SELECT }, { ACT_BACK, S_HINT_BACK } };
        ui_hints_draw(a, h, a->setmenu.depth > 0 ? 2 : 1);
    }
}
