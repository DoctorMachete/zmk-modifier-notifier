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
 * IMPLEMENTATION NOTES:
 *   We do NOT subscribe to zmk_modifiers_state_changed -- that event struct
 *   exists in ZMK headers but historically nothing in core ZMK actually
 *   raises it (see ZMK issue #144). Subscribing compiles fine but the
 *   listener never fires. Instead we subscribe to zmk_keycode_state_changed
 *   (raised on every keypress) and check the explicit modifier mask via
 *   zmk_hid_get_explicit_mods().
 *
 *   Settle/debounce: we do NOT read the modifier mask inline in the listener.
 *   Around a single keypress the explicit-mods mask can flip through transient
 *   intermediate values -- most visibly with sticky keys (e.g. &sk / quick-
 *   release sticky mods), which arm and release their modifier across the
 *   press/release of nearby keys. Reading inline samples whichever transient
 *   happens to be live at that sub-instant, and -- worse -- commits it to
 *   `last_sent_mods`. If the genuine settled mask later equals a transient we
 *   already stored, the dedup guard suppresses the correct packet, and since
 *   we only re-check on keycode events, the host stays frozen on a stale value
 *   until the next keypress.
 *
 *   Deferring the read to a short delayable work item (rescheduled on every
 *   event within the window) makes us sample the mask once it has settled, and
 *   dedups against that settled value -- eliminating both the stale sample and
 *   the stale-dedup freeze, regardless of what else shares the event bus.
 *
 *   The buffer is file-scope static; it is only ever touched from the settle
 *   work handler (system work queue, single-threaded), and send_report in
 *   zmk-raw-hid copies it before returning, so reuse is safe.
 */

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#include <raw_hid/events.h>

#include <string.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MODNOTIF_PACKET_MARKER 0xF2

/* Settle window: coalesce a burst of mod-mask changes into one send of the
 * final state. Mirrors the layer notifier. Tune if needed. */
#define MODNOTIF_SETTLE_MS 8

/* Persistent buffer -- see note above. */
static uint8_t hid_buf[CONFIG_RAW_HID_REPORT_SIZE];

/* Last sent mask; suppresses duplicate packets. Impossible initial value
 * forces the first send. */
static uint8_t last_sent_mods = 0xFF;

static void send_modifier_state(uint8_t mods) {
    memset(hid_buf, 0, sizeof(hid_buf));
    hid_buf[0] = MODNOTIF_PACKET_MARKER;
    hid_buf[1] = 1;
    hid_buf[2] = mods;

    LOG_DBG("Modifier notifier: mods=0x%02x", mods);

    raise_raw_hid_sent_event((struct raw_hid_sent_event){.data = hid_buf, .length = sizeof(hid_buf)});
}

static void modnotif_settle_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    /* Read the SETTLED mask and dedup against it. */
    uint8_t mods = (uint8_t)zmk_hid_get_explicit_mods();
    if (mods != last_sent_mods) {
        last_sent_mods = mods;
        send_modifier_state(mods);
    }
}

static K_WORK_DELAYABLE_DEFINE(modnotif_settle_work, modnotif_settle_work_handler);

static int keycode_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Reschedule the settle read on every event so a burst collapses into one
     * send of the final modifier state. */
    k_work_reschedule(&modnotif_settle_work, K_MSEC(MODNOTIF_SETTLE_MS));

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(modifier_notifier, keycode_state_changed_listener);
ZMK_SUBSCRIPTION(modifier_notifier, zmk_keycode_state_changed);
