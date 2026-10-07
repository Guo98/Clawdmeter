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
