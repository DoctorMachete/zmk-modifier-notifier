/*
 * zmk-modifier-notifier
 *
 * Sends a raw HID report whenever the explicit modifier state changes.
 *
 * Packet format (32 bytes):
 *   [0] = 0xF2  packet marker (distinct from 0xFF used by keypeek layer
 *                packets and 0xF1 used by keypeek key-position packets)
 *   [1] = 1     payload length in bytes
 *   [2] = modifier bitmask:
 *           bit 0: Left Ctrl   bit 4: Right Ctrl
 *           bit 1: Left Shift  bit 5: Right Shift
 *           bit 2: Left Alt    bit 6: Right Alt
 *           bit 3: Left GUI    bit 7: Right GUI
 *   [3..31] = 0
 *
 * The bitmask layout matches the standard USB HID keyboard modifier byte,
 * so left and right variants are preserved.
 *
 * NOTE: raise_raw_hid_sent_event is processed asynchronously by the raw_hid
 * adapter, so the data pointer must remain valid after this function returns.
 * The buffer is file-scope static (NOT stack-local) for this reason. The
 * keypeek module does the same thing for the same reason.
 */

#include <zmk/event_manager.h>
#include <zmk/events/modifiers_state_changed.h>
#include <zmk/hid.h>

#include <raw_hid/events.h>

#include <string.h>
#include <stdint.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MODNOTIF_PACKET_MARKER 0xF2

/* File-scope buffer — must persist after send_modifier_state returns because
 * raise_raw_hid_sent_event is consumed asynchronously by the raw_hid adapter. */
static uint8_t hid_buf[CONFIG_RAW_HID_REPORT_SIZE];

static void send_modifier_state(void) {
    /* zmk_hid_get_explicit_mods() returns the standard USB HID modifier byte
     * with handedness preserved. */
    zmk_mod_flags_t mods = zmk_hid_get_explicit_mods();

    memset(hid_buf, 0, sizeof(hid_buf));
    hid_buf[0] = MODNOTIF_PACKET_MARKER;
    hid_buf[1] = 1;
    hid_buf[2] = (uint8_t)mods;

    LOG_DBG("Modifier notifier: mods=0x%02x", mods);

    raise_raw_hid_sent_event((struct raw_hid_sent_event){.data = hid_buf, .length = sizeof(hid_buf)});
}

static int modifiers_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_modifiers_state_changed *ev = as_zmk_modifiers_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    send_modifier_state();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(modifier_notifier, modifiers_state_changed_listener);
ZMK_SUBSCRIPTION(modifier_notifier, zmk_modifiers_state_changed);
