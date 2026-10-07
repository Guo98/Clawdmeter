#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_CODEX,
    SCREEN_GITHUB,
    SCREEN_MEDIA,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);

// Media screen
void ui_update_now_playing(const NowPlaying* np);
int  ui_media_art_px(void);                    // cover edge length for this panel
void ui_set_media_art(const uint8_t* rgb565);  // px×px RGB565 LE, must stay valid

// Codex screen
void ui_update_codex(const CodexUsage* cx);

// GitHub screen
void ui_update_github(const GithubNotifs* gh);
