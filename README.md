# preface
This whole project and repository was written with Claude Opus 5.5 xHigh and Gemini 3.8 Flash High. Please take your time to verify facts written by them. I am not responsible for anything wrong that happens.

# re-fun60-ultra-tmr

Open firmware and a web driver for the MonsGeek FUN60 Ultra TMR Tri-mode (board ID 2307), written from
scratch after taking the stock firmware apart.

Two things started this:

- **Double typing with mechanical switches.** Put mechanical switches in this board and hard
  taps type the letter twice. Light taps are fine. The debounce slider in MonsGeek's app changes
  nothing, and there is no fixed firmware: v306 (2025-03-14) is still the latest for this board.
- **The MonsGeek web driver.** It's a pain to use and barely works on Linux.

It turned into a full replacement firmware. It's still a work in progress, so read the status
list before you flash anything.

## Status

Working, tested on my board:

- USB typing with the stock layout, plus the Fn layer (F keys, media, arrows)
- **no more double typing**: 0 doubles in 121 hard taps, where stock did 13 to 165 per run
  ([numbers below](#the-double-typing))
- per-key RGB: static, breathe, rainbow, brightness, on/off
- **2.4G wireless** through the stock dongle (Fn + Y). A dongle paired under stock just works.
- stock's indicator lights: the mode key blinks or stays lit, battery level on Esc
  (Fn + Space), charging, low battery, Caps Lock, Win lock (Fn + Win)
- battery level and charging state
- settings saved on the keyboard: keymap, lighting, debounce, thresholds
- FUN60 Studio, a web driver: remap, lighting, performance, save and restore, a double-press tester
- recovery without opening the case (hold Esc while plugging in), plus a watchdog failsafe

Built, not tested yet:

- Bluetooth 1 to 3 (Fn + E, R, T; hold 3 s to pair, it shows up as "FUN60 Ultra-1/2/3")

Not done:

- **sleep.** On battery the chip runs flat out, and only the LEDs turn off after 5 minutes idle.
  Don't leave it on battery for days.
- the web driver over 2.4G (USB cable only for now)
- Mac mode: the switch's top position is read but ignored
- NKRO: 6 keys at once, like a boot keyboard
- magnetic switch features: rapid trigger, per-key actuation, calibration. The firmware treats
  every key as a plain switch with one threshold tuned for mechanical switches, so the stock
  magnetic switches probably won't work right.
- the MonsGeek app will not work with this firmware, and that's not planned

## The double typing

The runs below are logs from `scripts/chatter_test.py`: me hammering one key as hard as I could.
A "double" is the same key reported again less than 30 ms after its release. The logs are in
`data/chatter/`, and you can re-check them with
`python3 scripts/chatter_test.py --analyze data/chatter/<file>`.

| Log | Firmware | Presses | Doubles |
|---|---|---|---|
| `chatter-20260928-181930.csv` (D) | stock v306 | 122 | 13 |
| `chatter-20260928-182151.csv` (H) | stock v306 | 157 | 34 |
| `chatter-20260928-182547.csv` (G) | stock v306 | 235 | 113 |
| `chatter-20260928-183235.csv` (G) | stock v306 | 302 | 165 |
| `chatter-20260928-203353.csv` (G) | this firmware | 121 | 0 |

Here's what's going on. On a hard tap, the contact opens for 0.1 to 6 ms in the middle of the
press, and flickers when it first closes. Stock reports every one of those as a new key press.
This firmware reports the press the moment the contact closes, but only reports the release once
the contact has stayed open for 8 ms. QMK calls this `asym_eager_defer_pk`. You get no added
delay on presses, and bounces can't turn into extra presses. The 8 ms can be changed in the
driver (0 turns it off).

## Build and flash

Everything below uses nix-shell; any Linux with the same tools works too. Flashing goes through
the keyboard's own bootloader over USB, so there's no need to open the case.

```sh
# firmware -> fw/build/fun60_dfu.bin (fetches Artery's BSP into fw/vendor the first time)
nix-shell -p gcc-arm-embedded gnumake git --run 'make -C fw vendor && make -C fw'
nix-shell -p gcc --run 'make -C fw test'          # host tests

# flasher
cd flasher && nix-shell -p cargo rustc pkg-config udev openssl --run 'cargo build --release' && cd ..

# flash (the keyboard can be on stock or on this firmware)
sudo flasher/target/release/ry-flash --image fw/build/fun60_dfu.bin --custom --arm
```

Back to stock (downloads v306 from MonsGeek's server and flashes it):

```sh
sudo flasher/target/release/ry-flash --auto --stock-dir stock --arm
```

## If it goes wrong

- Hold **Esc** while plugging the cable in: the keyboard goes straight to its bootloader
  (`lsusb` shows `3151:502a`), then flash again. This needs a working mechanical switch on Esc.
- A build that hangs, or never talks to the PC in its first 10 s, drops itself into the
  bootloader. The watchdog handles hangs; a "has a host ever answered" check handles the rest.
- If all of that fails, you open the case and bridge one pad. That's in
  [docs/recovery.md](docs/recovery.md). I've had to do it once.

## Keys

The layout is stock's. Fn layer:

| Fn + | Does |
|---|---|
| Esc, 1 to =, Backspace | `, F1 to F12, Delete |
| I, P | Insert, Print Screen |
| E, R, T / Y / U | Bluetooth 1, 2, 3 / 2.4G / USB (hold E R T Y 3 s to pair) |
| Enter, [, ], \ | lights on/off, dimmer, brighter, next effect |
| C, M, comma, period | Calculator, Mute, Volume down, Volume up |
| /, Right Alt, Menu, Right Ctrl | Up, Left, Down, Right |
| Win | Win lock |
| Space | battery level on Esc: red < 35 %, blue < 55, magenta < 75, yellow < 95, green |

The switch under the Caps Lock keycap has three positions. Top is wireless (Mac), bottom is
wireless (Windows), and the middle disconnects the battery. With the cable in, the keyboard
starts in USB mode. Unplug it (with the switch at the top or bottom) and it carries on in the
last wireless mode.

## Web driver

```sh
nix-shell -p python3 --run driver/serve.sh     # then open http://127.0.0.1:8060
```

It needs a Chromium-based browser (Chrome, Chromium, Brave) for WebHID, plus access to the
keyboard's hidraw node. A udev rule does that:

```
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3151", TAG+="uaccess"
```

On NixOS it goes in `services.udev.extraRules`; elsewhere it's a file in `/etc/udev/rules.d/`.
Without a keyboard, the driver runs a demo.

## Tools

- `scripts/chatter_test.py`: double-press tester. It reads the keyboard through evdev and logs
  every press and release to CSV. `--analyze` replays a log through different debounce filters.
- `scripts/rawscan.py`: talks to this firmware's vendor interface. With no options it shows the
  raw sensor grid live. `--keys` shows the keymap in RAM and what's held, `--power` battery and
  charging, `--radio` the wireless link, `--mode 24g` switches mode from the PC.
- `scripts/analyze_session.py`: makes sense of a `rawscan.py --record` capture.

## How it was made

The stock firmware (v306) was decompiled with Ghidra and read function by function. Every pin,
timing and protocol detail the firmware relies on is in [docs/hw-map.md](docs/hw-map.md), next
to the stock function it came from, so all of it can be checked. The code in `fw/` is new and
sits on Artery's own BSP. Nothing from MonsGeek is included.

The full story, including everything that went wrong, is in [docs/story.md](docs/story.md).

## Credits

- **[dot-agi/ry5088-flasher](https://github.com/dot-agi/ry5088-flasher)** by Pratyush Shukla (MIT):
  `ry-flash`, the USB flasher everything here is flashed with. It's in `flasher/` with a few
  changes, listed in its README. Its `firmware/` tree is not used; that's the one that bricked the
  board (see the story).
- **Artery Technology**: the AT32F402/405 BSP. `make vendor` fetches it, and four config files
  copied into `fw/src` keep Artery's license header.
- **NSA's Ghidra**, for the decompile.
- **QMK**, whose debounce docs name the algorithm used here.
- **[Sayobot's sayo-device-web-hid](https://github.com/Sayobot/sayo-device-web-hid)**, for how a
  WebHID config tool is put together. Wooting's Wootility, for the look to aim for. No code from
  either.
- **MonsGeek / RongYuan**: the stock firmware that was analysed. It isn't included here; ry-flash
  downloads it from their server when you want it back.
- **Claude** (Anthropic, in Claude Code) did the reverse engineering and wrote the firmware,
  driver, tools and docs. I did the hardware side, the testing, and kept telling it when it was
  wrong.
- **Gemini** flashed the build that bricked the keyboard, and is the reason the case had to come
  apart. See the story.

## License

MIT for everything here except where a file says otherwise. `flasher/` is MIT, Pratyush Shukla
(`flasher/LICENSE`). The Artery files keep Artery's license.
