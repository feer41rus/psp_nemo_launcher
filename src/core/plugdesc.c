#include "plugdesc.h"
#include <strings.h>

typedef struct { const char *file, *ru, *en; } PlugDesc;

static const PlugDesc DESCS[] = {
    { "nemo_cc.prx", "Control Center от Nemo: окно по HOME в игре — скриншот, громкость, яркость, выход",
      "Nemo Control Center: HOME window in games — screenshot, volume, brightness, exit" },
    { "leda.prx", "LEDA: запуск старых homebrew (ядро 1.50)", "LEDA: runs old homebrew (1.50 kernel)" },
    { "cwcheat.prx", "CWCheat: читы в играх", "CWCheat: cheats in games" },
    { "tempar.prx", "TempAR: читы и поиск значений в играх", "TempAR: cheats and value search in games" },
    { "pspstates.prx", "Сохранение и загрузка состояния игры", "Save and load game states" },
    { "pspstates_kai.prx", "Сохранение и загрузка состояния игры", "Save and load game states" },
    { "cxmb.prx", "CXMB: темы XMB", "CXMB: XMB themes" },
    { "cushome.prx", "Custom Home: своё меню по HOME в играх", "Custom Home: its own HOME menu in games" },
    { "satelite.prx", "Меню VSH в игре", "VSH menu in games" },
    { "categorylite.prx", "Категории игр в XMB", "Game categories in the XMB" },
    { "langswapper.prx", "Смена языка игры", "Changes the game language" },
    { "hold.prx", "Hold+: экономия заряда в режиме HOLD", "Hold+: saves battery in HOLD mode" },
    { "dayviewer.prx", "Дата и время в XMB", "Date and time in the XMB" },
    { "hud.prx", "PSP-HUD: FPS, заряд и время поверх игры", "PSP-HUD: FPS, battery and clock over games" },
    { "joysens.prx", "Настройка чувствительности стика", "Analog stick sensitivity" },
};

const char *plugdesc_get(const char *file_name, int lang_en) {
    for (unsigned i = 0; i < sizeof DESCS / sizeof DESCS[0]; i++)
        if (strcasecmp(file_name, DESCS[i].file) == 0) return lang_en ? DESCS[i].en : DESCS[i].ru;
    return lang_en ? "Third-party plugin — no description" : "Сторонний плагин — описание неизвестно";
}
