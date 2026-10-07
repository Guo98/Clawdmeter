#include "chime.h"
#include <Arduino.h>
#include "ESP_I2S.h"
#include "es8311.h"
#include "bell_pcm.h"   // const uint8_t bell_pcm[] / bell_pcm_len — 44.1 kHz 16-bit stereo

// Shared ES8311 chime engine. See chime.h. Adapted from the original 2.16
// sound.cpp so the 2.16, 1.8 (and any future ES8311 board) share one copy of
// the codec setup, the embedded PCM, and the non-blocking playback task.

static I2SClass      i2s;
static ChimeConfig   cfg;
static bool          ready   = false;
static volatile bool playing = false;

static bool es8311_setup(void) {
    es8311_handle_t es = es8311_create(0, cfg.es8311_addr);   // I2C port 0 (shared Wire bus)
    if (!es) return false;
    // mclk_inverted, sclk_inverted, mclk_from_mclk_pin, mclk_frequency, sample_frequency
    const es8311_clock_config_t clk = {
        false, false, true, cfg.sample_rate * 256, cfg.sample_rate
    };
    if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
    es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
    es8311_microphone_config(es, false);
    es8311_voice_volume_set(es, cfg.volume, NULL);
    return true;
}

// Synthesize one decaying sine note straight into I2S in small blocks, so the
// blip needs no PCM asset and only a 1 KB stack buffer.
static void write_note(float freq, int ms, float amp) {
    const int total = cfg.sample_rate * ms / 1000;
    const int attack = cfg.sample_rate / 400;           // 2.5 ms fade-in, no click
    int16_t buf[256 * 2];                                // 256 stereo frames
    float phase = 0.0f;
    const float step = 2.0f * (float)M_PI * freq / cfg.sample_rate;
    for (int n = 0; n < total; ) {
        int frames = min(256, total - n);
        for (int i = 0; i < frames; i++, n++) {
            float env = (n < attack) ? (float)n / attack
                                     : expf(-5.0f * (float)(n - attack) / total);
            int16_t v = (int16_t)(sinf(phase) * env * amp * 32767.0f);
            buf[2 * i] = buf[2 * i + 1] = v;
            phase += step;
            if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        i2s.write((uint8_t*)buf, frames * 4);
    }
}

enum clip_t { CLIP_BELL, CLIP_BLIP };

static void chime_task(void* arg) {
    clip_t clip = (clip_t)(uintptr_t)arg;
    if (cfg.amp_enable) cfg.amp_enable(true);
    delay(8);                                  // let the amp settle (avoids turn-on pop)
    if (clip == CLIP_BLIP) {
        // Two quick rising notes (E6 → A6): short and distinct from the bell.
        write_note(1318.5f, 70, 0.35f);
        write_note(0.0f, 15, 0.0f);            // silence between notes
        write_note(1760.0f, 110, 0.35f);
    } else {
        i2s.write((uint8_t*)bell_pcm, bell_pcm_len);
    }
    delay(20);
    if (cfg.amp_enable) cfg.amp_enable(false);
    playing = false;
    vTaskDelete(nullptr);
}

bool chime_init(const ChimeConfig& c) {
    cfg = c;
    if (cfg.amp_enable) cfg.amp_enable(false);   // amp off until we play

    i2s.setPins(cfg.bclk, cfg.ws, cfg.dout, cfg.din, cfg.mclk);
    if (!i2s.begin(I2S_MODE_STD, cfg.sample_rate, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("chime: I2S init failed");
        return false;
    }
    if (!es8311_setup()) {
        Serial.println("chime: ES8311 init failed");
        return false;
    }
    ready = true;
    Serial.println("chime: ES8311 ready");
    return true;
}

static void start_clip(clip_t clip) {
    if (!ready || playing) return;
    playing = true;
    if (xTaskCreatePinnedToCore(chime_task, "chime", 4096, (void*)(uintptr_t)clip, 1,
                                nullptr, 0) != pdPASS)
        playing = false;   // couldn't spawn — stay silent rather than wedge the flag
}

void chime_play(void)      { start_clip(CLIP_BELL); }
void chime_play_blip(void) { start_clip(CLIP_BLIP); }

void chime_tick(void) {}   // playback runs in chime_task; nothing to poll
