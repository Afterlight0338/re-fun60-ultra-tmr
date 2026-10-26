## 4. The brick

The short version: mechanical switches double typed on this board, an AI flashed a firmware that
bricked it, I opened it up and recovered it, and then the stock firmware got taken apart and
replaced. Here's the long version, mistakes included.

I started on 2026-09-27 from [dot-agi/ry5088-flasher](https://github.com/dot-agi/ry5088-flasher),
which has two parts: `ry-flash`, a USB flasher that talks to the keyboard's factory bootloader,
and a `firmware/` tree meant to be custom firmware for these boards. I had Gemini CLI add an
"eager debounce" to that firmware, build it and flash it.

The flash itself went fine: every chunk was transferred and the checksum matched. Then the keyboard
rebooted into the new firmware and never came back. No lights, and nothing on USB.

The cause was the build: `firmware/Makefile` compiled for `USB_OTG_FS`, the AT32F405's full-speed
USB controller on PA11/PA12. This board's USB-C port is wired to the other one, the high-speed
controller with its own PHY. So the firmware ran and brought up a USB port with nothing attached.
That setting was already in the upstream Makefile; nobody had run that tree on this board, and
Gemini flashed it without checking.

Why that's a brick: the factory bootloader at 0x08000000 only stays in flashing mode if a magic
word (0x55AA55AA at 0x08004800) is set, and the only thing that sets it is the running firmware,
when a flasher asks it over USB. A firmware without working USB can never be asked.

## 5. Recovery with the case open

Try the easy ways in [0.4](#0-how-to-flash) first. This is for when they don't work, for example
after a firmware that breaks USB, like the one that started all this. It's what got the board
back on 2026-09-28.

The AT32F405 has a ROM bootloader that nothing can overwrite. It runs instead of the flash when
the BOOT0 pin is high at power-up, and it speaks USB DFU. You can write the full stock image back
with it, factory bootloader included.

### 5.1 What you need

- a way to get the PCB out of the case (the pads are on the bottom side, with the switch sockets)
- a short wire or tweezers
- `dfu-util` (`nix-shell -p dfu-util`)
- the stock image. ry-flash downloads it from MonsGeek's server:
  `flasher/target/release/ry-flash --fetch 2307 --stock-dir stock` gives
  `stock/2307/fw_at32.bin` (110268 bytes, bootloader plus v306)

### 5.2 Steps

1. Unplug the cable and disconnect the battery connector.
2. Find the round pad silkscreened **BOOT1** just below the MCU (the chip marked RY5088). Despite
   the label, it's the chip's BOOT0 pin (pin 60).

   ![BOOT1 pad under the MCU](images/pcb-mcu-boot1.jpg)

3. Find the **VDD-3V3** pad next to the radio chip (PAN1080, U8), at the end of the SPI pads.

   ![VDD-3V3 pad next to the PAN1080](images/pcb-radio-3v3.jpg)

4. Bridge BOOT1 to VDD-3V3 and, while holding it, plug the cable in. Let go after a second.
5. Check that the ROM bootloader is there:
   ```
   lsusb | grep 2e3c:df11        # "Artery-Tech DFU in FS Mode"
   ```
6. Write the stock image:
   ```
   nix-shell -p dfu-util --run 'sudo "$(command -v dfu-util)" -a 0 -d 2e3c:df11 -s 0x08000000:leave -D stock/2307/fw_at32.bin'
   ```
   `:leave` restarts the chip when it's done, and the keyboard comes back on stock v306.
7. From there, ry-flash works over USB again, with no need to open the case. Put the battery back.

If `2e3c:df11` doesn't show up, the bridge wasn't making contact when the cable went in. Unplug
and try again.

## 6. The old firmware tree

With the board alive, Claude went through the `firmware/` tree (upstream plus Gemini's edits) to
see what else was wrong. Besides the USB port:

- it never switched on the sensor power rails, so every key read 0 (stock turns on PC14, then PC5
  20 ms later, then PA7 100 ms after that)
- the ADC was never given a trigger, so the first key read would hang forever
- one HID interface had no interrupt endpoint, which Linux's usbhid refuses
- the linker gave it 102 KB of RAM on a 96 KB chip
- the USB device version bytes were swapped
- it never set the bootloader mailbox, so ry-flash couldn't get back in either
- the pin map didn't match this board at all

That was enough to stop trusting any of it. The plan became: decompile the stock firmware, write
down what it actually does with every pin, and write new firmware on Artery's own BSP.

ry-flash itself worked, but it would flash any image with `--arm`. It now only arms the published
stock image for the connected board unless you add `--custom`, a hash mismatch on a downloaded
image is an error instead of a warning, and it says what to check when a board doesn't come back.

## 7. Writing new firmware

The stock firmware (v306) was decompiled with Ghidra and read function by function. Every pin,
timing and protocol detail the firmware relies on is in [section 12](#12-hardware-map), next to
the stock function it came from, so all of it can be checked. The code in `fw/` is new and sits
on Artery's own BSP. Nothing from MonsGeek is included.

Things that went wrong on the way to the first working build:

- **The USB "proof".** Before writing anything, Claude claimed a scan of the stock binary proved
  it uses the high-speed USB port. The scan didn't prove that (it looked for a value that is never
  stored as data). The real evidence was already in the kernel log: stock enumerates at high speed.
  The Makefile now refuses to produce an image without the high-speed interrupt handler in it.
- **A stale build.** v0.02 went out with old object files because the Makefile didn't track header
  changes, so it reported the wrong key count and version. Fixed with `-MMD -MP`.
- **Skipped columns.** The column shift register was clocked too fast. Whole columns glitched once
  a frame, and one clock was always lost at column 10. Waiting about 300 ns between edges fixed it.

The first working build typed the full layout and did 0 doubles in 121 hard taps. After that came
the LEDs (61 WS2812s fed by SPI2 and DMA), media keys, arrows, settings in flash, the failsafe, and
FUN60 Studio. At one point the driver said it couldn't save: that was v0.04 not having the save
command yet, and v0.05 fixed it.

## 8. Battery, radio, lights

All of this landed on 2026-09-30.

- **Battery.** It's read through the same ADC as the keys, on mux channel 7. That channel had read
  about 69 all along because the battery was unplugged.
- **Radio.** The 2.4G and Bluetooth side is a separate PAN1080 chip with its own firmware and its
  own pairings. The main chip only tells it which mode to be in and hands it HID reports, over SPI3
  with a small framed protocol (`[cmd][length][payload][sum]`). That was decoded from stock and
  rebuilt. The first status reply checked out on the first try.
- **Indicators.** Stock's colours live in its RAM init data, which the compiler stores compressed.
  They came out by transcribing stock's own decompressor. The same code also gave away the LED
  chain order: a snake, left to right on the top row, right to left on the next, and so on.

Two more mistakes:

- **The switch.** MonsGeek's guide says the switch under Caps Lock has wired in the middle, and
  stock picks wired mode from PC13. So for one version PC13 was treated as the switch. Measuring
  all three positions showed PC13 is simply USB power; the switch only changes PB1 (top = Mac), and
  its middle position disconnects the battery.
- **The Fn key.** None of the Fn combos worked, and it looked like a firmware bug for a while. A
  live view of what the keyboard thinks is held (`rawscan.py --keys`) showed Fn never closing, even
  though it had worked on 09-28. The switch was broken. A new switch fixed it.

After that: typing over 2.4G through the stock dongle, which had been paired under stock and
reconnected without pairing again.

## 9. Keys

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

With the cable in, the keyboard starts in USB mode. Unplug it (with the Caps Lock switch at the
top or bottom) and it carries on in the last wireless mode.

From v0.13 on, more of it is set in the web driver and saved on the keyboard:

- **Per key:** an actuation point, and rapid trigger. After its first press, a rapid-trigger key
  releases as soon as it comes up by the release amount and presses again when it goes down by
  the press amount, without the 8 ms release hold-off. All in percent of that key's own travel.
- **Dead zones:** travel near the top reads 0 %, near the bottom 100 %.
- **Calibration:** press every magnetic key all the way down once, then store it. The travel is
  saved; rest is still learned at power-on, so a key held down while plugging in reads 0 % until
  it is let go, and never types on its own.
- **NKRO** over USB, on a second keyboard report (interface 2, report ID 2). A BIOS asks for the
  boot protocol and gets the plain 6-key report. Off in Settings if something doesn't like it.
- **Advanced keys** (16): SnapKey / SOCD on two keys (last pressed wins, either one wins, or
  neither), mod-tap (tap for one key, hold for another), toggle (a tap latches it down), dynamic
  keystroke (up to 4 keys, each doing something at press, bottom, off the bottom and release),
  and macro keys.
- **Macros** (8, up to 32 steps each): key down, key up, tap, wait. Every key step takes 20 ms so
  each one reaches the PC as its own report; whatever a macro leaves down comes up at its end.
- **Profiles** (4): keymap, lighting, actuation, advanced keys and macros each. Calibration and the
  wireless mode are shared. Profile 1-4 keys can go anywhere in the keymap.

## 10. Web driver

FUN60 Studio, in `driver/`. It's hosted at **https://driver.vivlos.dev**, or run it locally:

```sh
nix-shell -p python3 --run driver/serve.sh     # then open http://127.0.0.1:8060
```

Pages: Device (battery, switching between USB, Bluetooth 1 to 3 and 2.4G, pairing), Keys
(base and Fn layers; click, Shift-click, drag a box or press keys on the keyboard to select;
swap, presets), Travel (actuation, dead zones, per-key actuation and rapid trigger, calibration,
live depth of every key, a travel graph for one key), Advanced (SnapKey, mod-tap, toggle, DKS,
macros with a recorder), Lighting (effects, speed, colour, per-key colours), Test (press every key
once, double-press tester), Firmware (version list, downloads, a browser flasher) and Settings
(save, profiles, NKRO, backup, theme). Pages for features a firmware doesn't have say which
version they need. Without a keyboard it runs a demo that your own keyboard drives.

It needs a Chromium-based browser (Chrome, Chromium, Brave) for WebHID, plus access to the
keyboard's hidraw node. A udev rule does that:

```
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3151", TAG+="uaccess"
```

On NixOS it goes in `services.udev.extraRules`; elsewhere it's a file in `/etc/udev/rules.d/`.
Without a keyboard, the driver runs a demo.

## 11. Tools

- `scripts/chatter_test.py`: double-press tester. It reads the keyboard through evdev and logs
  every press and release to CSV. `--analyze` replays a log through different debounce filters.
- `scripts/rawscan.py`: talks to this firmware's vendor interface. With no options it shows the
  raw sensor grid live. `--keys` shows the keymap in RAM and what's held, `--power` battery and
  charging, `--radio` the wireless link, `--mode 24g` switches mode from the PC.
- `scripts/analyze_session.py`: makes sense of a `rawscan.py --record` capture.
- `scripts/build_readme.py`, `scripts/build_site_page.py`: build this README from `docs/src/`, and
  the vivlos.dev page from it and `docs/short.md`.

