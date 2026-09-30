# What happened

The short version: mechanical switches double typed on this board, an AI flashed a firmware that
bricked it, I opened it up and recovered it, and then the stock firmware got taken apart and
replaced. Here's the long version, mistakes included.

## The problem

The FUN60 Ultra TMR ships with magnetic (TMR) switches, but the sockets take mechanical ones too.
With mechanical switches, hard taps sometimes type the letter twice. Light taps never do. The
debounce setting in MonsGeek's web driver doesn't help (plenty of people complain about it), and
MonsGeek's own advice is to pick the switch type and recalibrate. Their firmware server still lists
v306 from 2025-03-14 as the latest for this board.

## 2026-09-27: the brick

I started from [dot-agi/ry5088-flasher](https://github.com/dot-agi/ry5088-flasher), which has two
parts: `ry-flash`, a USB flasher that talks to the keyboard's factory bootloader, and a `firmware/`
tree meant to be custom firmware for these boards. I had Gemini CLI add an "eager debounce" to that
firmware, build it and flash it.

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

## 2026-09-28: recovery

The AT32F405 also has a ROM bootloader that can't be overwritten, selected by the BOOT0 pin at
power-up. On this PCB, BOOT0 goes to a round pad under the MCU silkscreened **BOOT1** (yes, the
label is off by one). I bridged it to the VDD-3V3 pad next to the radio chip and plugged in. The
board came up as `2e3c:df11` (Artery DFU), and `dfu-util` wrote the stock image back. Step by step:
[recovery.md](recovery.md).

## Looking at that firmware tree

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

## Measuring the double typing

Before fixing anything I wanted numbers, so `scripts/chatter_test.py` logs every press and release
from the keyboard with timestamps and flags re-presses under 30 ms. On stock v306, hammering single
keys gave 13, 34, 113 and 165 doubles per run (the table is in the README; logs in `data/chatter/`).

Claude's first reading was that the switch was bad. I didn't buy it: I swapped switches, and on
every one light taps were clean and hard taps doubled. So the logs got a closer look. On a hard tap
the contact opens for 0.1 to 6 ms in the middle of the press and flickers as it closes, and stock
reports each of those as a new press. That's a firmware problem, and the fix is the debounce
described in the README: report presses at once, and hold releases back for 8 ms.

The tester's first "how long a lockout would you need" formula was also wrong for bursts of
bounces. It was replaced by replaying each log through real filters and counting what gets through.

## Our own firmware

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

## 2026-09-30: battery, radio, lights

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

## Next

Sleep is the big one. The chip still runs at full speed on battery. After that: testing
Bluetooth, the driver over 2.4G, Mac mode, NKRO, and maybe support for the magnetic switches.
