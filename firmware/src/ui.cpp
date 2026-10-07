#include "ui.h"
#include "splash.h"
#include <lvgl.h>
#include <time.h>
#include "logo.h"
#include "clawd_still.h"
#include "icons.h"
#include "hal/board_caps.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_18);

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;
    int16_t bar_h;
    int16_t panel_pad_x, panel_pad_y;
    int16_t pill_pad_x, pill_pad_y;
    const lv_font_t* title_font;     // screen title / clock
    const lv_font_t* pct_font;       // big percentage number
    const lv_font_t* ent_pct_font;   // enterprise spending number
    const lv_font_t* pill_font;      // "Current" / "Weekly" pill
    const lv_font_t* reset_font;     // "Resets in ..." line
    const lv_font_t* pace_font;      // enterprise "Under/On/Over pace" line
    const lv_font_t* anim_font;      // animated status line
    int16_t anim_y;                  // status line offset from bottom
    bool    small_icons;             // 40px logo + 24px battery (vs 80/48) on small screens
    int16_t title_nudge;             // title x-shift balancing the corner logo
    int16_t logo_y;                  // logo top edge
    int16_t batt_y;                  // battery icon top edge
    int16_t batt_w;                  // battery icon width, for position math

    // Pairing hint / idle screen
    int16_t pair_y1, pair_y2, pair_y3;
    int16_t idle_px;                 // sleeping-creature size on the idle screen

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.margin = 20;
    L.title_y = 30;

    // Values shared by the two original breakpoints; the small branch below
    // overrides them wholesale.
    L.bar_h = 24;
    L.panel_pad_x = 16;
    L.panel_pad_y = 12;
    L.pill_pad_x = 18;
    L.pill_pad_y = 6;
    L.title_font   = &font_tiempos_56;
    L.pct_font     = &font_styrene_48;
    L.ent_pct_font = &font_tiempos_56;
    L.pill_font    = &font_styrene_28;
    L.reset_font   = &font_styrene_28;
    L.pace_font    = &font_styrene_16;
    L.anim_font    = &font_mono_32;
    L.anim_y = -15;
    L.small_icons = false;
    L.title_nudge = 16;
    L.logo_y = L.title_y - 10;
    L.batt_y = L.title_y;
    L.batt_w = ICON_BATTERY_W;
    L.pair_y1 = 40;
    L.pair_y2 = 120;
    L.pair_y3 = 160;
    L.idle_px = 160;

    if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.usage_panel_h = 150;
        L.usage_panel_gap = 16;
        L.usage_bar_y = 56;
        L.usage_reset_y = 94;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_tiempos_56;
        L.bt_status_font   = &font_styrene_48;
        L.bt_device_font   = &font_styrene_28;
        L.bt_credit_1_font = &font_styrene_24;
        L.bt_credit_2_font = &font_styrene_20;
    } else if (c.height >= 300) {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_28;
        L.bt_device_font   = &font_styrene_20;
        L.bt_credit_1_font = &font_styrene_16;
        L.bt_credit_2_font = &font_styrene_14;
    } else {
        // Small layout — tuned for 240x240 (LCD-1.54 and similar square TFTs).
        // Everything shrinks: fonts two steps down, panels ~half height, and
        // the corner logo/battery switch to the 40px/24px small assets.
        L.margin = 8;
        L.title_y = 4;
        L.content_y = 44;
        L.usage_panel_h = 74;
        L.usage_panel_gap = 6;
        L.usage_bar_y = 30;
        L.usage_reset_y = 46;
        L.bar_h = 12;
        L.panel_pad_x = 10;
        L.panel_pad_y = 6;
        L.pill_pad_x = 8;
        L.pill_pad_y = 2;
        L.title_font   = &font_tiempos_34;
        L.pct_font     = &font_styrene_24;
        L.ent_pct_font = &font_tiempos_34;
        L.pill_font    = &font_styrene_14;
        L.reset_font   = &font_styrene_14;
        L.pace_font    = &font_styrene_12;
        L.anim_font    = &font_mono_18;
        // Center the status line in the strip below the weekly panel; flush
        // against the bottom edge it reads as unevenly spaced.
        L.anim_y = -10;
        L.small_icons = true;
        L.title_nudge = 8;
        L.logo_y = 2;
        L.batt_y = 10;
        L.batt_w = ICON_BATTERY_SMALL_W;
        L.pair_y1 = 12;
        L.pair_y2 = 56;
        L.pair_y3 = 80;
        L.idle_px = 96;
        L.bt_info_panel_h = 90;
        L.bt_reset_zone_h = 60;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_20;
        L.bt_device_font   = &font_styrene_14;
        L.bt_credit_1_font = &font_styrene_12;
        L.bt_credit_2_font = &font_styrene_12;
    }

    L.content_w = L.scr_w - 2 * L.margin;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Usage screen widgets (single non-splash view) ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
// Clock fed by the daemon: base epoch (local wall-clock seconds) + the lv_tick at
// which it landed, so the title ticks forward locally between 60s payloads.
static long     clock_base_epoch = 0;
static uint32_t clock_base_ms = 0;
static int      clock_fmt = 24;   // 12 or 24, set from the daemon payload
static int      clock_last_min = -1;   // last rendered minute; avoids redrawing the title every tick
static lv_obj_t* usage_group;   // the two usage panels — shown when connected
static lv_obj_t* pair_group;    // pairing hint — shown when disconnected
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* panel_session = nullptr;
static lv_obj_t* panel_weekly = nullptr;
// Enterprise-only widgets inside panel_session
static lv_obj_t* lbl_session_pct_sym = nullptr;  // "%" in smaller font
static lv_obj_t* lbl_spending_desc = nullptr;     // "of your monthly budget"
static lv_obj_t* lbl_spending_status = nullptr;   // "Under pace" / "On pace" / "Over pace"
static lv_obj_t* lbl_anim;      // status line: connection state + whimsical idle

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// ---- Live-data freshness → which usage sub-view to show ----
// usage panels when data is flowing, an idle "Zzz" screen when the host is
// connected but no usage update landed within DATA_FRESH_MS, the pairing hint
// when BLE is down. Re-evaluated every loop in ui_tick_anim().
static lv_obj_t* idle_group;            // the "Zzz" idle screen
static uint32_t  last_data_ms = 0;      // lv_tick when the last valid usage update landed
static bool      data_received = false; // any valid update since boot
static bool      data_ok = true;        // last payload's ok flag; a {"ok":false} beat = "no fresh data"
static int       view_state = -1;       // -1 unknown / 0 pair / 1 idle / 2 usage
static const uint32_t DATA_FRESH_MS = 90000;  // usage counts as "live" within this window (daemon sends ~60s)

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;
static bool     s_ble_connected = false;   // cached BLE connection state
static uint32_t connected_at_ms = 0;       // when we last entered CONNECTED ("Connected" dwell)

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);
static void gesture_cb(lv_event_t* e);

// LVGL still sends CLICKED on release after a swipe; tap handlers check this
// so a swipe that starts on a button doesn't also press it.
static bool tap_was_swipe(void) {
    lv_indev_t* indev = lv_indev_active();
    return indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;
}

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_right(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_top(panel, L.panel_pad_y, 0);
    lv_obj_set_style_pad_bottom(panel, L.panel_pad_y, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, L.pill_font, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_right(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_top(lbl, L.pill_pad_y, 0);
    lv_obj_set_style_pad_bottom(lbl, L.pill_pad_y, 0);
    return lbl;
}

static void init_battery_icons(void) {
    if (L.small_icons) {
        init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_SMALL_W, ICON_BATTERY_SMALL_H, icon_battery_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_SMALL_W, ICON_BATTERY_LOW_SMALL_H, icon_battery_low_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_SMALL_W, ICON_BATTERY_MEDIUM_SMALL_H, icon_battery_medium_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_SMALL_W, ICON_BATTERY_FULL_SMALL_H, icon_battery_full_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_SMALL_W, ICON_BATTERY_CHARGING_SMALL_H, icon_battery_charging_small_data);
        return;
    }
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Usage Screen ========

static lv_obj_t* make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                                  lv_obj_t** out_pct, lv_obj_t** out_pill,
                                  lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, L.margin, y, L.content_w, L.usage_panel_h);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, L.pct_font, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    *out_pill = make_pill(panel, pill_text);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, L.usage_bar_y,
                        L.content_w - 2 * L.panel_pad_x, L.bar_h);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, L.reset_font, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);

    return panel;
}

// Pairing hint — shown when disconnected so the screen isn't empty and the
// user knows how to (re)pair. Wording matches the 3-second release gesture.
static void build_pair_group(lv_obj_t* parent) {
    pair_group = lv_obj_create(parent);
    lv_obj_set_size(pair_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(pair_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(pair_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pair_group, 0, 0);
    lv_obj_set_style_pad_all(pair_group, 0, 0);
    lv_obj_clear_flag(pair_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l1 = lv_label_create(pair_group);
    lv_label_set_text(l1, "To pair");
    lv_obj_set_style_text_font(l1, L.bt_status_font, 0);
    lv_obj_set_style_text_color(l1, COL_TEXT, 0);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, L.pair_y1);

    lv_obj_t* l2 = lv_label_create(pair_group);
    lv_label_set_text(l2, "hold the power button");
    lv_obj_set_style_text_font(l2, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l2, COL_DIM, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, L.pair_y2);

    lv_obj_t* l3 = lv_label_create(pair_group);
    lv_label_set_text(l3, "for 3 seconds, then release");
    lv_obj_set_style_text_font(l3, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l3, COL_DIM, 0);
    lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, L.pair_y3);

    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);  // ui_update_ble_status decides
}

// Idle "Zzz" screen — shown when the host is connected but no usage update has
// landed recently (token expired, daemon down, host asleep…). Full-screen, like
// the pairing hint, so we never render hours-old numbers as if they were live.
static void build_idle_group(lv_obj_t* parent) {
    idle_group = lv_obj_create(parent);
    lv_obj_set_size(idle_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(idle_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(idle_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_group, 0, 0);
    lv_obj_set_style_pad_all(idle_group, 0, 0);
    lv_obj_clear_flag(idle_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    // A shrunk-down resting creature (the official cloud-ride animation)
    // sits between the header and the status line; the animated "Listening…"
    // status line carries the words, so no extra text is needed here.
    lv_obj_t* creature = splash_mini_create(idle_group, "cloud", L.idle_px);
    if (creature) lv_obj_align(creature, LV_ALIGN_CENTER, 0, -20);

    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);  // update_view_state decides
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, L.title_font, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    // The nudge balances the corner logo on the left; smaller on small
    // screens where the logo is 40px and the battery icon sits closer.
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);

    // Usage panels (shown when connected) live in a transparent full-size group
    // so they can be toggled against the pairing hint as one unit.
    usage_group = lv_obj_create(usage_container);
    lv_obj_set_size(usage_group, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_group, 0, 0);
    lv_obj_set_style_bg_opa(usage_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_group, 0, 0);
    lv_obj_set_style_pad_all(usage_group, 0, 0);
    lv_obj_clear_flag(usage_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    panel_session = make_usage_panel(usage_group, L.content_y, "Current",
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset);

    // Enterprise-only overlays inside panel_session — hidden until enterprise data arrives
    lbl_session_pct_sym = lv_label_create(panel_session);
    lv_label_set_text(lbl_session_pct_sym, "%");
    lv_obj_set_style_text_font(lbl_session_pct_sym, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_session_pct_sym, COL_TEXT, 0);
    lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_desc = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_desc, "of your monthly budget");
    lv_obj_set_style_text_font(lbl_spending_desc, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_spending_desc, COL_DIM, 0);
    lv_obj_set_pos(lbl_spending_desc, 0, L.usage_reset_y);
    lv_obj_add_flag(lbl_spending_desc, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_status = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_status, "");
    lv_obj_set_style_text_font(lbl_spending_status, L.pace_font, 0);
    lv_obj_set_pos(lbl_spending_status, 0, L.usage_reset_y + 20);
    lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);

    panel_weekly = make_usage_panel(usage_group,
                     L.content_y + L.usage_panel_h + L.usage_panel_gap, "Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);
    // Recolor enabled so enterprise period box can color pace and reset separately
    lv_label_set_recolor(lbl_weekly_reset, true);

    build_pair_group(usage_container);
    build_idle_group(usage_container);

    // Status line — always visible on the usage view. Driven by ui_tick_anim().
    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, L.anim_font, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, L.anim_y);
}

// ---- Codex screen: OpenAI Codex 5-hour + weekly limits, same panels as Usage ----
static lv_obj_t* codex_container;
static lv_obj_t* codex_group;       // the two panels, hidden until data arrives
static lv_obj_t* lbl_codex_empty;
static lv_obj_t *lbl_cx_session_pct, *lbl_cx_session_pill, *bar_cx_session, *lbl_cx_session_reset;
static lv_obj_t *lbl_cx_weekly_pct,  *lbl_cx_weekly_pill,  *bar_cx_weekly,  *lbl_cx_weekly_reset;

static void init_codex_screen(lv_obj_t* scr) {
    codex_container = lv_obj_create(scr);
    lv_obj_set_size(codex_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(codex_container, 0, 0);
    lv_obj_set_style_bg_opa(codex_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(codex_container, 0, 0);
    lv_obj_set_style_pad_all(codex_container, 0, 0);
    lv_obj_clear_flag(codex_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(codex_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(codex_container);
    lv_label_set_text(title, "Codex");
    lv_obj_set_style_text_font(title, L.title_font, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);

    codex_group = lv_obj_create(codex_container);
    lv_obj_set_size(codex_group, L.scr_w, L.scr_h);
    lv_obj_set_pos(codex_group, 0, 0);
    lv_obj_set_style_bg_opa(codex_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(codex_group, 0, 0);
    lv_obj_set_style_pad_all(codex_group, 0, 0);
    lv_obj_clear_flag(codex_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(codex_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    make_usage_panel(codex_group, L.content_y, "Current",
                     &lbl_cx_session_pct, &lbl_cx_session_pill,
                     &bar_cx_session, &lbl_cx_session_reset);
    make_usage_panel(codex_group, L.content_y + L.usage_panel_h + L.usage_panel_gap, "Weekly",
                     &lbl_cx_weekly_pct, &lbl_cx_weekly_pill,
                     &bar_cx_weekly, &lbl_cx_weekly_reset);
    lv_obj_add_flag(codex_group, LV_OBJ_FLAG_HIDDEN);

    // The daemon only sends Codex limits once Codex has been used on the host.
    lbl_codex_empty = lv_label_create(codex_container);
    lv_label_set_text(lbl_codex_empty, "No Codex data yet");
    lv_obj_set_style_text_font(lbl_codex_empty, L.bt_device_font, 0);
    lv_obj_set_style_text_color(lbl_codex_empty, COL_DIM, 0);
    lv_obj_align(lbl_codex_empty, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(codex_container, LV_OBJ_FLAG_HIDDEN);
}

static void set_codex_panel(lv_obj_t* pct, lv_obj_t* bar, lv_obj_t* reset,
                            float value, int reset_mins) {
    int v = (int)(value + 0.5f);
    char buf[48];
    lv_label_set_text_fmt(pct, "%d%%", v);
    lv_bar_set_value(bar, v, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar, pct_color(value), LV_PART_INDICATOR);
    format_reset_time(reset_mins, buf, sizeof(buf));
    lv_label_set_text(reset, buf);
}

// ---- GitHub screen: newest unread PR notifications that involve the user ----
static lv_obj_t* github_container;
static lv_obj_t* lbl_github_title;
static lv_obj_t* lbl_github_count;   // pill beside the title
static lv_obj_t* lbl_github_empty;
static lv_obj_t* gh_list;            // vertically scrollable column of rows
static lv_obj_t* gh_row[GH_MAX_ITEMS];
static lv_obj_t* gh_reason[GH_MAX_ITEMS];
static lv_obj_t* gh_ref[GH_MAX_ITEMS];
static lv_obj_t* gh_title[GH_MAX_ITEMS];

// Tapping a row asks the daemon to open that PR in the Mac's browser.
static void gh_row_cb(lv_event_t* e) {
    if (tap_was_swipe()) return;
    ble_send_command(CMD_GH_OPEN + (uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void init_github_screen(lv_obj_t* scr) {
    github_container = lv_obj_create(scr);
    lv_obj_set_size(github_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(github_container, 0, 0);
    lv_obj_set_style_bg_opa(github_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(github_container, 0, 0);
    lv_obj_set_style_pad_all(github_container, 0, 0);
    lv_obj_clear_flag(github_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(github_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_github_title = lv_label_create(github_container);
    lv_label_set_text(lbl_github_title, "PRs");
    lv_obj_set_style_text_font(lbl_github_title, L.title_font, 0);
    lv_obj_set_style_text_color(lbl_github_title, COL_TEXT, 0);
    lv_obj_align(lbl_github_title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);

    lbl_github_count = make_pill(github_container, "");
    lv_obj_set_style_bg_color(lbl_github_count, COL_ACCENT, 0);
    lv_obj_add_flag(lbl_github_count, LV_OBJ_FLAG_HIDDEN);

    // One panel per notification: reason + repo#num on top, PR title below.
    const lv_font_t* meta_font  = L.bt_credit_2_font;   // 20 / 14 / 12
    const lv_font_t* title_font = L.bt_credit_1_font;   // 24 / 16 / 12
    const int32_t meta_h = lv_font_get_line_height(meta_font);
    const int32_t row_h = 2 * L.panel_pad_y + meta_h + 4 + lv_font_get_line_height(title_font);
    const int32_t gap = L.usage_panel_gap / 2;
    const int32_t inner_w = L.content_w - 2 * L.panel_pad_x;

    // Rows live in a column that scrolls vertically; horizontal swipes still
    // reach the screen-switch gesture since nothing scrolls sideways.
    gh_list = lv_obj_create(github_container);
    lv_obj_remove_style_all(gh_list);
    lv_obj_set_pos(gh_list, L.margin, L.content_y);
    lv_obj_set_size(gh_list, L.content_w, L.scr_h - L.content_y - L.margin);
    lv_obj_set_flex_flow(gh_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(gh_list, gap, 0);
    lv_obj_set_scroll_dir(gh_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(gh_list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_add_event_cb(gh_list, global_click_cb, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < GH_MAX_ITEMS; i++) {
        gh_row[i] = make_panel(gh_list, 0, 0, L.content_w, row_h);
        // Rows take the tap themselves (no bubbling to the screen cycle).
        lv_obj_remove_flag(gh_row[i], LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_flag(gh_row[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(gh_row[i], lv_color_lighten(COL_PANEL, LV_OPA_20), LV_STATE_PRESSED);
        lv_obj_add_event_cb(gh_row[i], gh_row_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

        gh_reason[i] = lv_label_create(gh_row[i]);
        lv_obj_set_style_text_font(gh_reason[i], meta_font, 0);
        lv_obj_set_pos(gh_reason[i], 0, 0);

        gh_ref[i] = lv_label_create(gh_row[i]);
        lv_obj_set_style_text_font(gh_ref[i], meta_font, 0);
        lv_obj_set_style_text_color(gh_ref[i], COL_DIM, 0);
        lv_obj_set_width(gh_ref[i], inner_w * 6 / 10);
        lv_label_set_long_mode(gh_ref[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(gh_ref[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_height(gh_ref[i], meta_h);
        lv_obj_align(gh_ref[i], LV_ALIGN_TOP_RIGHT, 0, 0);

        gh_title[i] = lv_label_create(gh_row[i]);
        lv_obj_set_style_text_font(gh_title[i], title_font, 0);
        lv_obj_set_style_text_color(gh_title[i], COL_TEXT, 0);
        lv_obj_set_width(gh_title[i], inner_w);
        lv_obj_set_height(gh_title[i], lv_font_get_line_height(title_font));
        lv_label_set_long_mode(gh_title[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_pos(gh_title[i], 0, meta_h + 4);

        lv_obj_add_flag(gh_row[i], LV_OBJ_FLAG_HIDDEN);
    }

    lbl_github_empty = lv_label_create(github_container);
    lv_label_set_text(lbl_github_empty, "No PR notifications");
    lv_obj_set_style_text_font(lbl_github_empty, L.bt_device_font, 0);
    lv_obj_set_style_text_color(lbl_github_empty, COL_DIM, 0);
    lv_obj_align(lbl_github_empty, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(github_container, LV_OBJ_FLAG_HIDDEN);
}

// ---- Media screen: previous / play-pause / next as BLE HID media keys ----
static lv_obj_t* media_container;
static lv_obj_t* media_art_box;     // rounded placeholder; clips the cover
static lv_obj_t* media_art_img;
static lv_obj_t* lbl_media_title;
static lv_obj_t* lbl_media_artist;
static lv_obj_t* media_play_btn;
static lv_image_dsc_t media_art_dsc;
static int  media_art_px_val = 0;
static int  media_state = -1;       // np_state_t, or -1 before the daemon reports
static NowPlaying media_np = {};
static bool media_np_received = false;

static void fill_rect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = COL_TEXT;
    d.bg_opa = LV_OPA_COVER;
    d.radius = 2;
    lv_area_t a = {x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

static void fill_tri(lv_layer_t* layer, int32_t ax, int32_t ay, int32_t bx, int32_t by,
                     int32_t cx, int32_t cy) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = COL_TEXT;
    d.opa = LV_OPA_COVER;
    d.p[0].x = ax; d.p[0].y = ay;
    d.p[1].x = bx; d.p[1].y = by;
    d.p[2].x = cx; d.p[2].y = cy;
    lv_draw_triangle(layer, &d);
}

// Icons are drawn as shapes rather than font glyphs: the bundled fonts have no
// media symbols, and an icon font big enough to read across a desk would cost
// far more flash. Geometry is in units of 1/10 of the button width so the same
// code scales from the 1.54" panel to the 2.16".
static void media_icon_draw_cb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    uint16_t usage = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    lv_area_t c;
    lv_obj_get_coords(btn, &c);
    const int32_t cx = (c.x1 + c.x2) / 2;
    const int32_t cy = (c.y1 + c.y2) / 2;
    const int32_t u  = lv_area_get_width(&c) / 10;
    const int32_t h  = u * 16 / 10;   // icon half-height
    const int32_t bw = u * 6 / 10;    // bar width

    if (usage == MEDIA_PLAY_PAUSE && media_state == NP_PLAYING) {
        // ❚❚ — tapping pauses
        const int32_t gap = u * 6 / 10;
        fill_rect(layer, cx - gap / 2 - u, cy - h, cx - gap / 2, cy + h);
        fill_rect(layer, cx + gap / 2, cy - h, cx + gap / 2 + u, cy + h);
    } else if (usage == MEDIA_PLAY_PAUSE && media_state >= 0) {
        // ▶ — nudged right so it looks optically centered
        const int32_t x0 = cx - h * 8 / 10;
        fill_tri(layer, x0, cy - h, x0, cy + h, x0 + h * 2, cy);
    } else if (usage == MEDIA_PLAY_PAUSE) {
        // ▶❚❚ — state unknown (no song info yet), so show both.
        const int32_t x0 = cx - u * 23 / 10;
        fill_tri(layer, x0, cy - h, x0, cy + h, x0 + u * 26 / 10, cy);
        const int32_t b1 = x0 + u * 31 / 10;
        fill_rect(layer, b1, cy - h, b1 + bw, cy + h);
        fill_rect(layer, b1 + bw + u * 4 / 10, cy - h, b1 + 2 * bw + u * 4 / 10, cy + h);
    } else if (usage == MEDIA_NEXT) {
        // ▶❚
        fill_tri(layer, cx - h, cy - h, cx - h, cy + h, cx + h - bw, cy);
        fill_rect(layer, cx + h - bw, cy - h, cx + h, cy + h);
    } else {
        // ❚◀
        fill_rect(layer, cx - h, cy - h, cx - h + bw, cy + h);
        fill_tri(layer, cx + h, cy - h, cx + h, cy + h, cx - h + bw, cy);
    }
}

static void media_btn_cb(lv_event_t* e) {
    if (tap_was_swipe()) return;
    uint16_t usage = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    ble_media_tap(usage);
    // Flip the icon right away; the daemon's next poll (~3 s) confirms it.
    if (usage == MEDIA_PLAY_PAUSE && media_state >= NP_PAUSED) {
        media_state = (media_state == NP_PLAYING) ? NP_PAUSED : NP_PLAYING;
        lv_obj_invalidate(media_play_btn);
    }
}

// Title/artist lines, by priority: link down → no info yet → idle → track.
static void media_refresh_text(void) {
    if (!lbl_media_title) return;
    const char* title = "Spotify";
    const char* artist = "";
    if (!s_ble_connected)                 title = "Not connected";
    else if (!media_np_received)          title = "Spotify";
    else if (media_np.state == NP_OFF)    title = "Nothing playing";
    else { title = media_np.title; artist = media_np.artist; }
    // set_text restarts the circular scroll, so only touch changed labels.
    if (strcmp(lv_label_get_text(lbl_media_title), title) != 0)
        lv_label_set_text(lbl_media_title, title);
    if (strcmp(lv_label_get_text(lbl_media_artist), artist) != 0)
        lv_label_set_text(lbl_media_artist, artist);
}

static lv_obj_t* make_media_btn(lv_obj_t* parent, int32_t size, uint16_t usage, bool primary) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);   // drop the default theme's shadow/outline
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_color_t base = primary ? COL_ACCENT : COL_PANEL;
    lv_obj_set_style_bg_color(btn, base, 0);
    lv_obj_set_style_bg_color(btn, primary ? lv_color_darken(base, LV_OPA_30)
                                           : lv_color_lighten(base, LV_OPA_20),
                              LV_STATE_PRESSED);
    // No EVENT_BUBBLE: a tap on a button must not also cycle the screen.
    lv_obj_add_event_cb(btn, media_btn_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)usage);
    lv_obj_add_event_cb(btn, media_icon_draw_cb, LV_EVENT_DRAW_MAIN_END, (void*)(uintptr_t)usage);
    return btn;
}

static void init_media_screen(lv_obj_t* scr) {
    media_container = lv_obj_create(scr);
    lv_obj_set_size(media_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(media_container, 0, 0);
    lv_obj_set_style_bg_opa(media_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(media_container, 0, 0);
    lv_obj_set_style_pad_all(media_container, 0, 0);
    lv_obj_clear_flag(media_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(media_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    // Album art at the top, between the corner mascot and the battery.
    // Multiple of 8 so the daemon's resize stays crisp: 168 / 152 / 80 px.
    const int32_t art = (L.scr_h * 35 / 100) & ~7;
    media_art_px_val = art;
    media_art_box = lv_obj_create(media_container);
    lv_obj_remove_style_all(media_art_box);
    lv_obj_set_size(media_art_box, art, art);
    lv_obj_align(media_art_box, LV_ALIGN_TOP_MID, 0, L.title_y);
    lv_obj_set_style_bg_color(media_art_box, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(media_art_box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(media_art_box, art / 14, 0);
    lv_obj_set_style_clip_corner(media_art_box, true, 0);
    lv_obj_add_flag(media_art_box, LV_OBJ_FLAG_EVENT_BUBBLE);

    media_art_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    media_art_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    media_art_dsc.header.w = art;
    media_art_dsc.header.h = art;
    media_art_dsc.header.stride = art * 2;
    media_art_dsc.data_size = art * art * 2;
    media_art_img = lv_image_create(media_art_box);
    lv_obj_set_pos(media_art_img, 0, 0);
    lv_obj_add_flag(media_art_img, LV_OBJ_FLAG_HIDDEN);   // until a cover lands
    lv_obj_add_flag(media_art_img, LV_OBJ_FLAG_EVENT_BUBBLE);

    const lv_font_t* title_font  = L.bt_device_font;     // 28 / 20 / 14
    const lv_font_t* artist_font = L.bt_credit_2_font;   // 20 / 14 / 12
    const int32_t text_y = L.title_y + art + art / 10;

    lbl_media_title = lv_label_create(media_container);
    lv_label_set_text(lbl_media_title, "");
    lv_obj_set_width(lbl_media_title, L.content_w);
    lv_label_set_long_mode(lbl_media_title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_media_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_media_title, title_font, 0);
    lv_obj_set_style_text_color(lbl_media_title, COL_TEXT, 0);
    lv_obj_align(lbl_media_title, LV_ALIGN_TOP_MID, 0, text_y);

    lbl_media_artist = lv_label_create(media_container);
    lv_label_set_text(lbl_media_artist, "");
    lv_obj_set_width(lbl_media_artist, L.content_w);
    lv_label_set_long_mode(lbl_media_artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(lbl_media_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_media_artist, artist_font, 0);
    lv_obj_set_style_text_color(lbl_media_artist, COL_DIM, 0);
    lv_obj_align(lbl_media_artist, LV_ALIGN_TOP_MID, 0,
                 text_y + lv_font_get_line_height(title_font) + 4);

    // prev · play/pause · next along the bottom; play/pause is the big accent button.
    const int32_t big   = L.scr_w * 26 / 100;
    const int32_t small = L.scr_w * 18 / 100;
    const int32_t gap   = L.scr_w * 5 / 100;
    const int32_t row_y = -(L.scr_h * 5 / 100);
    media_play_btn = make_media_btn(media_container, big, MEDIA_PLAY_PAUSE, true);
    lv_obj_align(media_play_btn, LV_ALIGN_BOTTOM_MID, 0, row_y);
    lv_obj_t* prev = make_media_btn(media_container, small, MEDIA_PREV, false);
    lv_obj_align_to(prev, media_play_btn, LV_ALIGN_OUT_LEFT_MID, -gap, 0);
    lv_obj_t* next = make_media_btn(media_container, small, MEDIA_NEXT, false);
    lv_obj_align_to(next, media_play_btn, LV_ALIGN_OUT_RIGHT_MID, gap, 0);

    media_refresh_text();
    lv_obj_add_flag(media_container, LV_OBJ_FLAG_HIDDEN);
}

// ---- Zoom screen: mic / camera toggles, executed by the daemon ----
// Taps go to the daemon as one-byte commands; it clicks Zoom's own menu items
// (no focus steal) and reports the resulting state back as {"zm":{...}}.
static lv_obj_t* zoom_container;
static lv_obj_t* lbl_zoom_status;
static lv_obj_t* zoom_mic_btn;
static lv_obj_t* zoom_cam_btn;
static lv_obj_t* lbl_zoom_mic;
static lv_obj_t* lbl_zoom_cam;
static ZoomStatus zoom_st = {};
static bool zoom_received = false;

static void draw_line(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                      int32_t w, lv_color_t col) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = col;
    d.width = w;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = x1; d.p1.y = y1;
    d.p2.x = x2; d.p2.y = y2;
    lv_draw_line(layer, &d);
}

// Mic and camera glyphs, drawn like the media icons. Struck through when off.
static void zoom_icon_draw_cb(lv_event_t* e) {
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const bool is_mic = (btn == zoom_mic_btn);
    lv_area_t c;
    lv_obj_get_coords(btn, &c);
    const int32_t w = lv_area_get_width(&c), h = lv_area_get_height(&c);
    const int32_t u  = LV_MIN(w, h) / 14;
    const int32_t cx = (c.x1 + c.x2) / 2;
    const int32_t cy = c.y1 + h * 42 / 100;   // above the caption
    const int32_t sw = LV_MAX(u * 5 / 10, 2);  // stroke width
    const bool off = is_mic ? zoom_st.muted : !zoom_st.video;

    if (is_mic) {
        // capsule + cradle + stem + base
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.bg_color = COL_TEXT;
        r.bg_opa = LV_OPA_COVER;
        r.radius = LV_RADIUS_CIRCLE;
        lv_area_t cap = {cx - u * 12 / 10, cy - u * 32 / 10, cx + u * 12 / 10, cy + u * 8 / 10};
        lv_draw_rect(layer, &r, &cap);

        lv_draw_arc_dsc_t a;
        lv_draw_arc_dsc_init(&a);
        a.color = COL_TEXT;
        a.width = sw;
        a.rounded = 1;
        a.center.x = cx;
        a.center.y = cy - u * 2 / 10;
        a.radius = u * 21 / 10;
        a.start_angle = 0;
        a.end_angle = 180;
        lv_draw_arc(layer, &a);

        const int32_t stem_top = a.center.y + a.radius;
        draw_line(layer, cx, stem_top, cx, stem_top + u, sw, COL_TEXT);
        draw_line(layer, cx - u * 12 / 10, stem_top + u, cx + u * 12 / 10, stem_top + u, sw, COL_TEXT);
    } else {
        // camera body + lens wedge
        lv_draw_rect_dsc_t r;
        lv_draw_rect_dsc_init(&r);
        r.bg_color = COL_TEXT;
        r.bg_opa = LV_OPA_COVER;
        r.radius = u / 2;
        lv_area_t body = {cx - u * 30 / 10, cy - u * 18 / 10, cx + u * 10 / 10, cy + u * 18 / 10};
        lv_draw_rect(layer, &r, &body);
        fill_tri(layer, cx + u * 14 / 10, cy, cx + u * 30 / 10, cy - u * 16 / 10,
                 cx + u * 30 / 10, cy + u * 16 / 10);
    }

    if (off) {
        // Slash with a background-colored halo so it reads as cut through.
        lv_color_t bg = lv_obj_get_style_bg_color(btn, LV_PART_MAIN);
        draw_line(layer, cx - u * 30 / 10, cy - u * 32 / 10, cx + u * 30 / 10, cy + u * 32 / 10,
                  sw * 3, bg);
        draw_line(layer, cx - u * 30 / 10, cy - u * 32 / 10, cx + u * 30 / 10, cy + u * 32 / 10,
                  sw, COL_TEXT);
    }
}

// Status line, captions, colors and enabled state from the latest report.
static void zoom_refresh(void) {
    if (!lbl_zoom_status) return;
    const char* status;
    lv_color_t status_col = COL_DIM;
    bool in_meeting = false;
    if (!s_ble_connected)                         status = "Not connected";
    else if (!zoom_received)                      status = "Waiting for daemon";
    else switch (zoom_st.state) {
        case ZOOM_MEETING:   status = "In meeting"; status_col = COL_GREEN; in_meeting = true; break;
        case ZOOM_IDLE:      status = "Not in a meeting"; break;
        case ZOOM_NO_ACCESS: status = "Daemon needs Accessibility"; status_col = COL_AMBER; break;
        default:             status = "Zoom isn't running"; break;
    }
    lv_label_set_text(lbl_zoom_status, status);
    lv_obj_set_style_text_color(lbl_zoom_status, status_col, 0);

    // Captions state what *is*, not what a tap does: "Unmute" on a muted
    // tile read as "you're unmuted". Red + a slashed icon also mean "off".
    lv_label_set_text(lbl_zoom_mic, zoom_st.muted ? "Muted" : "Mic on");
    lv_label_set_text(lbl_zoom_cam, zoom_st.video ? "Camera on" : "Camera off");
    lv_obj_set_style_bg_color(zoom_mic_btn, (in_meeting && zoom_st.muted) ? COL_RED : COL_PANEL, 0);
    lv_obj_set_style_bg_color(zoom_cam_btn, (in_meeting && !zoom_st.video) ? COL_RED : COL_PANEL, 0);

    lv_obj_t* btns[] = {zoom_mic_btn, zoom_cam_btn};
    for (lv_obj_t* b : btns) {
        if (in_meeting) lv_obj_remove_state(b, LV_STATE_DISABLED);
        else            lv_obj_add_state(b, LV_STATE_DISABLED);
        lv_obj_set_style_opa(b, in_meeting ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_obj_invalidate(b);
    }
}

static void zoom_btn_cb(lv_event_t* e) {
    if (tap_was_swipe()) return;
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_current_target(e);
    if (zoom_st.state != ZOOM_MEETING) return;
    // Flip right away; the daemon re-reads Zoom's state and confirms.
    if (btn == zoom_mic_btn) {
        ble_send_command(CMD_ZOOM_MIC);
        zoom_st.muted = !zoom_st.muted;
    } else {
        ble_send_command(CMD_ZOOM_VIDEO);
        zoom_st.video = !zoom_st.video;
    }
    zoom_refresh();
}

static lv_obj_t* make_zoom_btn(lv_obj_t* parent, int32_t w, int32_t h, lv_color_t base,
                               lv_obj_t** caption, const lv_font_t* font) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, L.scr_w / 24, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, base, 0);
    lv_obj_set_style_bg_color(btn, lv_color_lighten(base, LV_OPA_20), LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, zoom_btn_cb, LV_EVENT_CLICKED, NULL);

    *caption = lv_label_create(btn);
    lv_label_set_text(*caption, "");
    lv_obj_set_style_text_font(*caption, font, 0);
    lv_obj_set_style_text_color(*caption, COL_TEXT, 0);
    return btn;
}

static void init_zoom_screen(lv_obj_t* scr) {
    zoom_container = lv_obj_create(scr);
    lv_obj_set_size(zoom_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(zoom_container, 0, 0);
    lv_obj_set_style_bg_opa(zoom_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(zoom_container, 0, 0);
    lv_obj_set_style_pad_all(zoom_container, 0, 0);
    lv_obj_clear_flag(zoom_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(zoom_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(zoom_container);
    lv_label_set_text(title, "Zoom");
    lv_obj_set_style_text_font(title, L.title_font, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);

    const lv_font_t* status_font  = L.bt_credit_1_font;   // 24 / 16 / 12
    const lv_font_t* caption_font = L.bt_credit_1_font;
    lbl_zoom_status = lv_label_create(zoom_container);
    lv_label_set_text(lbl_zoom_status, "");
    lv_obj_set_width(lbl_zoom_status, L.content_w);
    lv_label_set_long_mode(lbl_zoom_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(lbl_zoom_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl_zoom_status, status_font, 0);
    lv_obj_align(lbl_zoom_status, LV_ALIGN_TOP_MID, 0, L.content_y);

    // Mic and camera tiles side by side, leaving a strip below to tap through.
    const int32_t gap     = L.usage_panel_gap;
    const int32_t bottom  = L.scr_h * 10 / 100;
    const int32_t tiles_y = L.content_y + lv_font_get_line_height(status_font) + gap;
    const int32_t tile_h  = L.scr_h - bottom - tiles_y;
    const int32_t tile_w  = (L.content_w - gap) / 2;

    zoom_mic_btn = make_zoom_btn(zoom_container, tile_w, tile_h, COL_PANEL, &lbl_zoom_mic, caption_font);
    lv_obj_set_pos(zoom_mic_btn, L.margin, tiles_y);
    zoom_cam_btn = make_zoom_btn(zoom_container, tile_w, tile_h, COL_PANEL, &lbl_zoom_cam, caption_font);
    lv_obj_set_pos(zoom_cam_btn, L.margin + tile_w + gap, tiles_y);
    for (lv_obj_t* b : {zoom_mic_btn, zoom_cam_btn}) {
        lv_obj_add_event_cb(b, zoom_icon_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_BOTTOM_MID, 0, -tile_h / 10);
    }

    zoom_refresh();
    lv_obj_add_flag(zoom_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Public API ========

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

#ifndef BOARD_HAS_PSRAM
    // Static corner mascot (see clawd_still.h) — the animated one needs PSRAM.
    if (L.small_icons) init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_SMALL_W, CLAWD_STILL_SMALL_H, clawd_still_small_data);
    else               init_icon_dsc_rgb565a8(&logo_dsc, CLAWD_STILL_W, CLAWD_STILL_H, clawd_still_data);
#endif
    init_battery_icons();

    init_usage_screen(scr);
    init_codex_screen(scr);
    init_github_screen(scr);
    init_media_screen(scr);
    init_zoom_screen(scr);
    lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, NULL);
    splash_init(scr);

    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    // Corner mascot in the old logo slot. The still Clawd is shorter than the
    // 80/40 px slot the spark logo used; center it vertically in that slot.
    {
        const int slot  = L.small_icons ? LOGO_SMALL_HEIGHT : LOGO_HEIGHT;
        const int art_h = L.small_icons ? CLAWD_STILL_SMALL_H : CLAWD_STILL_H;
        const int top   = L.logo_y + (slot - art_h) / 2;
#ifdef BOARD_HAS_PSRAM
        // Animated: idles, does acts, and takes walk-off/lurk trips.
        splash_mascot_create(scr, L.margin, top + art_h, L.small_icons ? 2 : 3);
#else
        logo_img = lv_image_create(scr);
        lv_image_set_src(logo_img, &logo_dsc);
        lv_obj_set_pos(logo_img, L.margin, top);
#endif
    }

    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - L.batt_w - L.margin, L.batt_y);
    // Boards without battery telemetry never show the indicator (per the HAL
    // contract; previously every board drew the empty-battery glyph).
    if (!board_caps().has_battery) {
        lv_obj_del(battery_img);
        battery_img = nullptr;
    }
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;
    data_ok = data->ok;
    if (!data->ok) return;          // a {"ok":false} "no data" beat → fall through to idle, keep last numbers
    last_data_ms = lv_tick_get();   // a real usage update just landed
    data_received = true;

    if (data->clock_epoch > 0) {    // daemon supplied wall-clock time → drive the title clock
        clock_base_epoch = data->clock_epoch;
        clock_base_ms = last_data_ms;
        clock_fmt = data->clock_fmt;
    } else if (clock_base_epoch != 0) {   // clock turned off daemon-side → revert title to "Usage"
        clock_base_epoch = 0;
        clock_last_min = -1;
        lv_label_set_text(lbl_title, "Usage");
    }

    int s_pct = (int)(data->session_pct + 0.5f);

    if (data->enterprise) {
        // Spending box: big number-only label + small "%" symbol + desc + pace
        lv_obj_set_style_text_font(lbl_session_pct, L.ent_pct_font, 0);
        lv_label_set_text(lbl_session_label, "Spending");
        lv_obj_add_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status,   LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_text_font(lbl_session_pct, L.pct_font, 0);
        lv_label_set_text(lbl_session_label, "Current");
        lv_obj_clear_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    }

    char buf[48];

    // Pace vars used in both enterprise blocks below
    const char* pace_text = "Under pace";
    lv_color_t  pace_color = COL_GREEN;
    const char* pace_hex   = "788c5d";   // matches THEME_GREEN
    if (data->session_pct > (float)data->time_pct + 15.0f) {
        pace_text = "Over pace";  pace_color = COL_RED;   pace_hex = "c0392b";
    } else if (data->session_pct > (float)data->time_pct - 15.0f) {
        pace_text = "On pace";    pace_color = COL_AMBER; pace_hex = "d97757";
    }

    if (data->enterprise) {
        lv_label_set_text_fmt(lbl_session_pct, "%d", s_pct);
        lv_obj_align_to(lbl_session_pct_sym, lbl_session_pct,
                        LV_ALIGN_OUT_RIGHT_TOP, 4, 12);
    } else {
        lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
        format_reset_time(data->session_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_session_reset, buf);
    }

    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);

    if (data->enterprise) {
        // Period box: time % + dynamic pace color + "Resets <date>" label
        lv_label_set_text(lbl_weekly_label, "Period");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", data->time_pct);
        lv_bar_set_value(bar_weekly, data->time_pct, LV_ANIM_ON);
        lv_color_t bar_pace = (data->session_pct <= (float)data->time_pct) ? COL_GREEN :
                              (data->session_pct <= (float)data->time_pct + 15.0f) ? COL_AMBER :
                              COL_RED;
        lv_obj_set_style_bg_color(bar_weekly, bar_pace, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "#%s %s# - #faf9f5 Resets %s#",
                 pace_hex, pace_text, data->reset_date);
        lv_label_set_text(lbl_weekly_reset, buf);
    } else {
        int w_pct = (int)(data->weekly_pct + 0.5f);
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
        lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);
        format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_reset, buf);
    }
}

// Pick the usage-view sub-screen: pairing hint (BLE down), the idle "Zzz" screen
// (connected but data has gone stale), or the live usage panels. Only re-lays-out
// on an actual change. The animated status line stays visible everywhere — it
// reads "Listening…" on the idle screen, keeping it alive rather than frozen.
static void update_view_state(void) {
    if (!usage_group || !pair_group || !idle_group) return;
    int v;
    if (!s_ble_connected) {
        v = 0;  // pairing hint
    } else if (data_received && data_ok && (lv_tick_get() - last_data_ms) < DATA_FRESH_MS) {
        v = 2;  // live usage
    } else {
        v = 1;  // idle / Zzz
    }
    if (v == view_state) return;
    view_state = v;
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v == 0 ? pair_group : v == 1 ? idle_group : usage_group,
                      LV_OBJ_FLAG_HIDDEN);
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;
    update_view_state();
    if (view_state == 1) splash_mini_tick();   // animate the sleeping creature on the idle screen

    uint32_t now = lv_tick_get();

    // Title clock: once the daemon has sent wall-clock time, replace "Usage" with
    // the live time, advanced locally so it ticks every minute between payloads.
    if (clock_base_epoch > 0) {
        time_t cur = (time_t)(clock_base_epoch + (now - clock_base_ms) / 1000);
        struct tm tmv;
        gmtime_r(&cur, &tmv);   // epoch is already local wall-clock → gmtime keeps it as-is
        if (tmv.tm_min != clock_last_min) {   // only rewrite the title when the minute changes
            clock_last_min = tmv.tm_min;
            char tbuf[12];
            if (clock_fmt == 12) {
                int h12 = tmv.tm_hour % 12;
                if (h12 == 0) h12 = 12;
                snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tmv.tm_min,
                         tmv.tm_hour < 12 ? "AM" : "PM");
            } else {
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            }
            lv_label_set_text(lbl_title, tbuf);
        }
    }

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms < spinner_ms[anim_spinner_idx]) return;
    anim_last_ms = now;
    anim_phase = (anim_phase + 1) % SPINNER_PHASES;
    anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                    : (SPINNER_PHASES - anim_phase);

    // Status text by priority. Whimsical messages only when connected & settled.
    const char* text;
    if (!s_ble_connected) {
        text = "Waiting";              // advertising / waiting for a host connection
    } else if (view_state == 1) {      // idle — alternate so it reads as alive AND data-less
        text = (anim_msg_idx & 1) ? "No data" : "Listening";
    } else if (now - connected_at_ms < 5000) {
        text = "Connected";
    } else {
        text = anim_messages[anim_msg_idx];
    }

    // All states share the whimsical style: "<glyph> <Title-case word>…"
    static char buf[80];
    snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
             spinner_frames[anim_spinner_idx], text);
    lv_label_set_text(lbl_anim, buf);
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

// Tapping empty space cycles splash → usage → codex → github → media → zoom → splash.
static void global_click_cb(lv_event_t* e) {
    (void)e;
    if (tap_was_swipe()) return;
    ui_show_screen((screen_t)((current_screen + 1) % SCREEN_COUNT));
}

// Swiping anywhere (buttons included) moves through the same cycle:
// swipe left for the next screen, right for the previous one.
static void gesture_cb(lv_event_t* e) {
    (void)e;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT)
        ui_show_screen((screen_t)((current_screen + 1) % SCREEN_COUNT));
    else if (dir == LV_DIR_RIGHT)
        ui_show_screen((screen_t)((current_screen + SCREEN_COUNT - 1) % SCREEN_COUNT));
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(codex_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(github_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(media_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(zoom_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:  splash_show(); break;
    case SCREEN_USAGE:   lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_CODEX:   lv_obj_clear_flag(codex_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_GITHUB:  lv_obj_clear_flag(github_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_MEDIA:   lv_obj_clear_flag(media_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_ZOOM:    lv_obj_clear_flag(zoom_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    splash_mascot_set_visible(screen != SCREEN_SPLASH);
    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_battery_visibility();
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char* name, const char* mac) {
    (void)name; (void)mac;
    bool was_connected = s_ble_connected;
    s_ble_connected = (state == BLE_STATE_CONNECTED);

    if (s_ble_connected && !was_connected) connected_at_ms = lv_tick_get();
    media_refresh_text();
    zoom_refresh();
    // pair / idle / usage — picked from connection + data freshness.
    update_view_state();
}

void ui_update_battery(int percent, bool charging) {
    if (!battery_img) return;
    int idx;
    if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}

// ---- Media screen API ----

void ui_update_now_playing(const NowPlaying* np) {
    media_np = *np;
    media_np_received = true;
    if (media_state != np->state) {
        media_state = np->state;
        lv_obj_invalidate(media_play_btn);
    }
    // Nothing playing → back to the empty placeholder; a new track keeps the
    // old cover until its own arrives (~1 s later) rather than flashing empty.
    if (np->state == NP_OFF) lv_obj_add_flag(media_art_img, LV_OBJ_FLAG_HIDDEN);
    media_refresh_text();
}

int ui_media_art_px(void) {
    return media_art_px_val;
}

void ui_set_media_art(const uint8_t* rgb565) {
    media_art_dsc.data = rgb565;
    lv_image_set_src(media_art_img, &media_art_dsc);
    lv_obj_clear_flag(media_art_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(media_art_img);
}

// ---- Codex screen API ----

void ui_update_codex(const CodexUsage* cx) {
    lv_obj_add_flag(lbl_codex_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(codex_group, LV_OBJ_FLAG_HIDDEN);
    set_codex_panel(lbl_cx_session_pct, bar_cx_session, lbl_cx_session_reset,
                    cx->session_pct, cx->session_reset_mins);
    set_codex_panel(lbl_cx_weekly_pct, bar_cx_weekly, lbl_cx_weekly_reset,
                    cx->weekly_pct, cx->weekly_reset_mins);
}

// ---- GitHub screen API ----

void ui_update_github(const GithubNotifs* gh) {
    // Count pill beside the title; the pair is centered where the title alone
    // sits, so it clears the corner mascot and battery icon.
    if (gh->total > 0) {
        if (gh->total > 99) lv_label_set_text(lbl_github_count, "99+");
        else                lv_label_set_text_fmt(lbl_github_count, "%d", gh->total);
        lv_obj_clear_flag(lbl_github_count, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(github_container);
        const int32_t gap = L.title_nudge / 2 + 4;
        const int32_t w = lv_obj_get_width(lbl_github_title) + gap + lv_obj_get_width(lbl_github_count);
        lv_obj_align(lbl_github_title, LV_ALIGN_TOP_MID,
                     L.title_nudge - (w - lv_obj_get_width(lbl_github_title)) / 2, L.title_y);
        lv_obj_align_to(lbl_github_count, lbl_github_title, LV_ALIGN_OUT_RIGHT_MID, gap, 0);
    } else {
        lv_obj_add_flag(lbl_github_count, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(lbl_github_title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);
    }

    for (int i = 0; i < GH_MAX_ITEMS; i++) {
        if (i >= gh->count) {
            lv_obj_add_flag(gh_row[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const GithubItem& it = gh->items[i];
        lv_label_set_text(gh_reason[i], it.reason);
        // Things waiting on the user stand out; FYI-type reasons stay dim.
        bool action = strcmp(it.reason, "Review") == 0 || strcmp(it.reason, "Mention") == 0;
        lv_obj_set_style_text_color(gh_reason[i], action ? COL_ACCENT : COL_DIM, 0);
        lv_label_set_text(gh_ref[i], it.ref);
        lv_label_set_text(gh_title[i], it.title);
        lv_obj_clear_flag(gh_row[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (gh->count > 0) lv_obj_add_flag(lbl_github_empty, LV_OBJ_FLAG_HIDDEN);
    else               lv_obj_clear_flag(lbl_github_empty, LV_OBJ_FLAG_HIDDEN);
}

// ---- Zoom screen API ----

void ui_update_zoom(const ZoomStatus* zm) {
    zoom_st = *zm;
    zoom_received = true;
    zoom_refresh();
}
