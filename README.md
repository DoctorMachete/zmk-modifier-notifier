# zmk-modifier-notifier

Sends a raw HID report on every change of the explicit modifier state, so a
host-side script can display which modifiers are currently active.

## Requirements

- `zmk-raw-hid` module (patched version)
- `CONFIG_RAW_HID=y` (or use the `raw_hid_adapter` shield)
- `CONFIG_ZMK_MODIFIER_NOTIFIER=y`

## Packet format (32 bytes)

| Byte    | Value | Meaning                                       |
|---------|-------|-----------------------------------------------|
| 0       | 0xF2  | Packet marker (modifier-state)                |
| 1       | 1     | Payload length in bytes                       |
| 2       | mods  | Modifier bitmask (see below)                  |
| 3..31   | 0     | Padding                                       |

### Modifier bitmask (byte 2)

This is the standard USB HID keyboard modifier byte, so left and right
variants are distinct.

| Bit | Modifier      |
|-----|---------------|
| 0   | Left Ctrl     |
| 1   | Left Shift    |
| 2   | Left Alt      |
| 3   | Left GUI      |
| 4   | Right Ctrl    |
| 5   | Right Shift   |
| 6   | Right Alt     |
| 7   | Right GUI     |

A value of 0x00 means no modifiers are active.

## Adding to west.yml

```yaml
- name: zmk-modifier-notifier
  remote: <your-remote>
  revision: main
```

## Why a snapshot, not a delta?

The packet contains the current full state, not a press/release event. This
means the host script can render directly from this byte without tracking
any state of its own. If the connection drops a packet, the next change
re-syncs the host automatically.

## Sticky / one-shot modifiers

Sticky modifiers appear in this bitmask the same as held modifiers -- once
the sticky engages, its bit is set in `zmk_hid_get_explicit_mods()`. The
host cannot distinguish "shift is held" from "shift is sticky-pending"
from this byte alone. A future version could add a second byte for
sticky-pending state if that distinction is needed.
