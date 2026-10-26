## 1. The board

| Item | Value |
| :--- | :--- |
| Keyboard | MonsGeek FUN60 Ultra TMR, 60%, 61 keys, per-key RGB |
| Board ID | 2307 (`dev_id` in MonsGeek's firmware server) |
| Stock firmware | v306 (2025-03-14), still the latest |
| MCU | Artery AT32F405RCT7, silkscreened "RY5088" |
| USB | `3151:5030` running (high speed), `3151:502a` in the bootloader (full speed) |
| Radio | PAN1080 for 2.4G and Bluetooth, with its own firmware and pairings |
| Switches | magnetic (TMR) from the factory; the sockets take mechanical ones too |

The switch under the Caps Lock keycap has three positions. Top is wireless (Mac), bottom is
wireless (Windows), and the middle disconnects the battery. Every pin and timing is in
[section 12](#12-hardware-map).

## 2. Status

Working, tested on my board:

- USB typing with the stock layout, plus the Fn layer (F keys, media, arrows)
- **no more double typing**: 0 doubles in 121 hard taps, where stock did 13 to 165 per run
  ([numbers below](#3-the-double-typing))
- per-key RGB: static, breathe, rainbow, brightness, on/off
- **2.4G wireless** through the stock dongle (Fn + Y). A dongle paired under stock just works.
- stock's indicator lights: the mode key blinks or stays lit, battery level on Esc
  (Fn + Space), charging, low battery, Caps Lock, Win lock (Fn + Win)
- battery level and charging state
- settings saved on the keyboard: keymap, lighting, debounce, actuation
- FUN60 Studio, a web driver at driver.vivlos.dev: remap, connection modes, live key travel,
  lighting, a press test and a double-press tester, save and restore
- recovery without opening the case (hold Esc while plugging in), plus a watchdog failsafe

Built, not tested yet:

- magnetic switches (v0.12): each key learns its rest and bottom like stock (bottom starts
  400 counts under rest), presses at a percent of that travel. Holding Esc while plugging in
  still uses mechanical thresholds, so with a magnetic Esc it may not reach the bootloader; the
  watchdog failsafe still does.
- Bluetooth 1 to 3 (Fn + E, R, T; hold 3 s to pair, it shows up as "FUN60 Ultra-1/2/3")

Not done:

- **sleep.** On battery the chip runs flat out, and only the LEDs turn off after 5 minutes idle.
  Don't leave it on battery for days.
- the web driver over 2.4G (USB cable only for now)
- Mac mode: the switch's top position is read but ignored
- NKRO: 6 keys at once, like a boot keyboard
- magnetic switch extras: rapid trigger, a different actuation per key, calibration that
  survives a power cycle, stock's travel-to-millimetre curve (actuation is a percent, not mm)
- the MonsGeek app will not work with this firmware, and that's not planned

## 3. The double typing

### 3.1 The problem

The FUN60 Ultra TMR ships with magnetic (TMR) switches, but the sockets take mechanical ones too.
With mechanical switches, hard taps sometimes type the letter twice. Light taps never do. The
debounce setting in MonsGeek's web driver doesn't help (plenty of people complain about it), and
MonsGeek's own advice is to pick the switch type and recalibrate. Their firmware server still lists
v306 from 2025-03-14 as the latest for this board.

### 3.2 Measuring it

Before fixing anything I wanted numbers, so `scripts/chatter_test.py` logs every press and release
from the keyboard with timestamps and flags re-presses under 30 ms. The runs below are me
hammering one key as hard as I could. A "double" is the same key reported again less than 30 ms
after its release. The logs are in `data/chatter/`, and you can re-check them with
`python3 scripts/chatter_test.py --analyze data/chatter/<file>`.

| Log | Firmware | Presses | Doubles |
|---|---|---|---|
| `chatter-20260928-181930.csv` (D) | stock v306 | 122 | 13 |
| `chatter-20260928-182151.csv` (H) | stock v306 | 157 | 34 |
| `chatter-20260928-182547.csv` (G) | stock v306 | 235 | 113 |
| `chatter-20260928-183235.csv` (G) | stock v306 | 302 | 165 |
| `chatter-20260928-203353.csv` (G) | this firmware | 121 | 0 |

Claude's first reading was that the switch was bad. I didn't buy it: I swapped switches, and on
every one light taps were clean and hard taps doubled. So the logs got a closer look.

### 3.3 What's going on, and the fix

On a hard tap, the contact opens for 0.1 to 6 ms in the middle of the press, and flickers when it
first closes. Stock reports every one of those as a new key press. That's a firmware problem.

This firmware reports the press the moment the contact closes, but only reports the release once
the contact has stayed open for 8 ms. QMK calls this `asym_eager_defer_pk`. You get no added
delay on presses, and bounces can't turn into extra presses. The 8 ms can be changed in the
driver (0 turns it off).

The tester's first "how long a lockout would you need" formula was wrong for bursts of bounces.
It was replaced by replaying each log through real filters and counting what gets through.

