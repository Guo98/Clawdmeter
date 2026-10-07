#pragma once
#include <Arduino.h>

struct UsageData {
    float session_pct;       // utilization 0-100 (5h window Pro/Max; spending % Enterprise)
    int session_reset_mins;  // minutes until reset
    float weekly_pct;        // 7-day utilization (Pro/Max only; 0 for Enterprise)
    int weekly_reset_mins;   // minutes until weekly reset (Pro/Max only)
    char status[16];         // "allowed", "limited", etc.
    bool chime;              // play the session-reset chime; false unless daemon opts in
    bool enterprise;         // true = Enterprise spending-limit account
    int time_pct;            // 0-100: fraction of billing period elapsed (Enterprise)
    int period_days;         // total billing period length in days (Enterprise)
    char reset_date[12];     // formatted reset date e.g. "Jul 1" (Enterprise)
    long clock_epoch;        // local wall-clock epoch (s) from daemon; 0 = not provided
    int  clock_fmt;          // 12 or 24 (hour format from daemon); defaults to 24
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};

// Now-playing info pushed by the daemon as {"np":{"s":..,"t":..,"a":..}}.
enum np_state_t {
    NP_OFF = 0,       // nothing playing / Spotify idle
    NP_PAUSED = 1,
    NP_PLAYING = 2,
};

struct NowPlaying {
    int  state;              // np_state_t
    char title[96];
    char artist[64];
};

// Codex (OpenAI) plan limits pushed by the daemon as {"cx":{"s","sr","w","wr"}}.
struct CodexUsage {
    float session_pct;       // 5-hour window, 0-100
    int   session_reset_mins;  // -1 = unknown (window lapsed since last Codex use)
    float weekly_pct;        // 7-day window, 0-100
    int   weekly_reset_mins;
};

// GitHub PRs pushed by the daemon in chunks of
// {"gh":{"n":reviews,"o":offset,"t":rows,"i":[[ref, title, reason], ...],"b":1?}}
// — rows o.. of t; "b" (first chunk only) asks for the review-request blip.
#define GH_MAX_ITEMS 12
struct GithubItem {
    char ref[40];            // "repo#123"
    char title[72];
    char reason[16];         // top-left label: the PR author
};

struct GithubNotifs {
    int total;               // open PRs awaiting the user's review
    int count;               // rows in the list, <= GH_MAX_ITEMS
    bool blip;               // a new review request arrived since the last poll
    GithubItem items[GH_MAX_ITEMS];
};

// Zoom meeting state pushed by the daemon as {"zm":{"s":..,"a":..,"v":..}}.
enum zoom_state_t {
    ZOOM_OFF = 0,          // Zoom not running
    ZOOM_IDLE = 1,         // running, not in a meeting
    ZOOM_MEETING = 2,
    ZOOM_NO_ACCESS = 3,    // daemon lacks macOS Accessibility permission
};

struct ZoomStatus {
    int  state;            // zoom_state_t
    bool muted;
    bool video;            // camera on
};
