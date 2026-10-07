#pragma once
#include <stdint.h>

enum ble_state_t {
    BLE_STATE_INIT,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTED,
};

void ble_init(void);
void ble_tick(void);
ble_state_t ble_get_state(void);
const char* ble_get_device_name(void);
const char* ble_get_mac_address(void);
void ble_clear_bonds(void);
bool ble_has_bonds(void);
bool ble_has_data(void);
const char* ble_get_data(void);
void ble_send_ack(void);
void ble_send_nack(void);
void ble_request_refresh(void);

// Device → daemon commands ride the refresh-request characteristic as a
// one-byte notify (0x01 stays "refresh"). Old daemons treat any value as a
// refresh, which is harmless.
#define CMD_ZOOM_MIC   0x10
#define CMD_ZOOM_VIDEO 0x11
#define CMD_GH_OPEN    0x20   // + row index: open that PR in the browser
void ble_send_command(uint8_t cmd);

void ble_set_battery_level(int pct);

// BLE HID keyboard
void ble_keyboard_press(uint8_t key, uint8_t modifier);
void ble_keyboard_release(void);

// BLE HID consumer control (media keys)
#define MEDIA_PLAY_PAUSE 0x00CD
#define MEDIA_NEXT       0x00B5
#define MEDIA_PREV       0x00B6
void ble_media_tap(uint16_t usage);

// Album art: allocate buffers for a px×px RGB565 cover and advertise the size
// to the daemon. ble_take_art() returns the newest complete cover (once), or
// nullptr; the pointer stays valid until the next call that returns non-null.
void ble_art_init(uint16_t px);
const uint8_t* ble_take_art(void);
