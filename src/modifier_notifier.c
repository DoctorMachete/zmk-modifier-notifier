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
 * Note: this sends a snapshot of the CURRENT explicit modifier state on each
 * change, not a delta. The host script can render directly from this byte
 * with no state tracking.
 *
 * Sticky / one-shot modifiers: when a sticky modifier engages, it appears in
 * the explicit modifier mask just like a regular held modifier, so it will
 * be reflected here. The host cannot distinguish "shift is held" from
 * "shift is sticky-pending" from this packet alone -- that would require a
 * more invasive integration with the sticky-key behavior. Left as future
 * work; for most indicator use cases the bitmask is sufficient.
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

static void send_modifier_state(void) {
    uint8_t buf[CONFIG_RAW_HID_REPORT_SIZE] = {0};

    /* zmk_hid_get_explicit_mods() returns the standard USB HID modifier byte
     * with handedness preserved. */
    zmk_mod_flags_t mods = zmk_hid_get_explicit_mods();

    buf[0] = MODNOTIF_PACKET_MARKER;
    buf[1] = 1;
    buf[2] = (uint8_t)mods;

    LOG_DBG("Modifier notifier: mods=0x%02x", mods);

    raise_raw_hid_sent_event((struct raw_hid_sent_event){.data = buf, .length = sizeof(buf)});
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
