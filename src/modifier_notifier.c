/*
 * zmk-modifier-notifier
 *
 * Sends a raw HID report whenever the explicit modifier state changes.
 *
 * Packet format (32 bytes):
 *   [0] = 0xF2  packet marker
 *   [1] = 1     payload length in bytes
 *   [2] = modifier bitmask (USB HID standard, handedness preserved)
 *           bit 0: Left Ctrl   bit 4: Right Ctrl
 *           bit 1: Left Shift  bit 5: Right Shift
 *           bit 2: Left Alt    bit 6: Right Alt
 *           bit 3: Left GUI    bit 7: Right GUI
 *   [3..31] = 0
 *
 * IMPORTANT IMPLEMENTATION NOTE:
 *   We do NOT subscribe to zmk_modifiers_state_changed -- that event struct
 *   exists in ZMK headers but historically nothing in core ZMK actually
 *   raises it (see ZMK issue #144). Subscribing compiles fine but the
 *   listener never fires.
 *
 *   Instead we subscribe to zmk_keycode_state_changed (which IS raised on
 *   every keypress) and after each event check the explicit modifier mask
 *   via zmk_hid_get_explicit_mods(). If the mask differs from what we
 *   last sent, we transmit a new packet.
 *
 *   The buffer is file-scope static because raise_raw_hid_sent_event is
 *   processed asynchronously and the data pointer must remain valid after
 *   our function returns.
 */

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#include <raw_hid/events.h>

#include <string.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MODNOTIF_PACKET_MARKER 0xF2

/* Persistent buffer -- see note above. */
static uint8_t hid_buf[CONFIG_RAW_HID_REPORT_SIZE];

/* Last sent mask; used to suppress duplicate packets when non-modifier keys change. */
static uint8_t last_sent_mods = 0xFF;   /* Impossible value forces first send */

static void send_modifier_state(uint8_t mods) {
    memset(hid_buf, 0, sizeof(hid_buf));
    hid_buf[0] = MODNOTIF_PACKET_MARKER;
    hid_buf[1] = 1;
    hid_buf[2] = mods;

    LOG_DBG("Modifier notifier: mods=0x%02x", mods);

    raise_raw_hid_sent_event((struct raw_hid_sent_event){.data = hid_buf, .length = sizeof(hid_buf)});
}

static int keycode_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* After every keycode event, check current modifier state.
     * Only send if it changed from what we last reported. */
    uint8_t mods = (uint8_t)zmk_hid_get_explicit_mods();
    if (mods != last_sent_mods) {
        last_sent_mods = mods;
        send_modifier_state(mods);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(modifier_notifier, keycode_state_changed_listener);
ZMK_SUBSCRIPTION(modifier_notifier, zmk_keycode_state_changed);
