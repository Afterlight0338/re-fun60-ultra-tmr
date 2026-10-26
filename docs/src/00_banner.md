<!-- banner:start -->
> [!CAUTION]
> **This entire project was done with LLMs:** Claude Opus 5.5 (medium and xHigh reasoning) for the
> reverse engineering, the firmware, the driver, the tools and these docs, and Gemini 3.8 / 3.7
> Flash (High) at the start, which is the one that bricked the keyboard. I did not check every
> byte of it, and I am **not responsible for anything in here that turns out to be wrong.** Some
> of it probably is. Flash something because a README told you to, turn your keyboard into a
> very expensive paperweight, and that is on you. Not me, not Claude, you.
>
> Read the code yourself. Check every pin and every command before you run it. Better yet, do
> the shi yourself and use this as a map, not a manual. **You have been warned.**

**Same thing, but shorter and just looks nicer i guess? → [vivlos.dev/fun60](https://vivlos.dev/fun60/)**
<!-- banner:end -->


# re-fun60-ultra-tmr: open firmware for the MonsGeek FUN60 Ultra TMR

Open firmware and a web driver for the MonsGeek FUN60 Ultra TMR (board ID 2307), written from
scratch after taking the stock firmware apart.

Two things started this:

- **Double typing with mechanical switches.** Put mechanical switches in this board and hard
  taps type the letter twice. Light taps are fine. The debounce slider in MonsGeek's app changes
  nothing, and there is no fixed firmware: v306 (2025-03-14) is still the latest for this board.
- **The MonsGeek web driver.** It's a pain to use and barely works on Linux.

It turned into a full replacement firmware. It's still a work in progress, so read the
[status list](#2-status) before you flash anything.

Written 2026-09-30, updated 2026-10-26 for firmware v0.17. Stock firmware analysed: v306 for board 2307.

> **v0.17 is experimental.** It ran on my board all night and every feature was checked on the
> real USB link, but by test tools pressing keys from the PC, not by fingers on keys. The magnetic
> part is built on real readings from two magnetic switches (Space and Fn), not a full magnetic
> board. If you just want a working keyboard, flash **v0.11** (`git checkout v0.11`, then build as
> below): mechanical switches only, typing every day on my board for a month. If v0.17 misbehaves,
> recovery is the same as always ([section 4](#4-the-brick)), but holding Esc while plugging in
> may not work with a magnetic Esc; the watchdog failsafe does.

## -2. Changelog

Newest first. Small wording fixes are not listed.

**2026-10-26 (night)**

* Firmware v0.13 to v0.17, **experimental**, each one flashed and checked on my board:
  * v0.13: per-key actuation point, rapid trigger (press and release sensitivity, optional
    continuous mode), top and bottom dead zones, and calibration that survives a power cycle
    (each key's travel is saved, its rest position is still learned at power-on).
  * v0.14: NKRO over USB (every key at once, on a second keyboard report, so a BIOS still gets
    the 6-key one), and advanced keys: SnapKey / SOCD, mod-tap, toggle and dynamic keystroke (DKS).
  * v0.15: four new lighting effects (Spectrum, Reactive, Ripple, Custom per-key colours) and an
    effect speed.
  * v0.16: macros, 8 of up to 32 steps, played from a key once or again while held.
  * v0.17: 4 profiles, switched from the driver or from Profile 1-4 keys you can put anywhere.
  * Settings reset to defaults once with each of these versions.
* First real measurements of magnetic switches on this board: a magnetic Space rests at 1806 and
  bottoms out at 1211, a magnetic Fn 1290 and 797, noise about 3 counts. The reading falls on
  press, as stock's maths assumes, so the v0.12 model holds. The capture is in `fw/test/`, and the
  host tests replay it through the real key code.
* FUN60 Studio: Advanced page (SnapKey, mod-tap, toggle, DKS, macros with a recorder), per-key
  actuation and rapid trigger on the Travel page, calibration (store, clear, relearn), new
  lighting effects and per-key colour painting, NKRO and profiles on the Settings page. With
  two tabs open, the last one to connect has the keyboard and the other says so, instead of both
  getting each other's replies.
* The browser flasher has now flashed a real keyboard: v0.11 and v0.12 from Brave, 2 s for the
  bootloader, under 2 s to write, settings put back after.

**2026-10-26**

* FUN60 Studio: Firmware page with a version list (v0.11 stable, v0.12 experimental), downloads
  for ry-flash, and an **experimental** browser flasher (a port of ry-flash; the command line
  stays the recommended way). It keeps your settings and offers to put them back after a flash.
* FUN60 Studio reworked after MonsGeek's own driver: Device, Keys, Travel, Lighting, Test and
  Settings pages; switch connection mode and pair from the page; box and press-to-select;
  live travel of every key. Hosted at https://driver.vivlos.dev.
* Firmware v0.12, **extremely experimental**: basic magnetic switch support, never run on a
  keyboard yet. The last tested firmware is tagged `v0.11`. Each key learns
  where it rests and how deep it goes, like stock does, and presses at a percent of that travel
  (50 % by default, released under 35 %). Mechanical and magnetic switches can be mixed. The web
  driver's thresholds are percents now (protocol v5). Saved settings reset to defaults once.

**2026-10-25**

* Docs reorganised into this one README (the old `docs/story.md`, `docs/hw-map.md` and
  `docs/recovery.md` are sections 3 to 8 and 12 now). Short version on vivlos.dev.
* Added 0.5: flashing from a virtual machine, from the first issue report.

**2026-09-30**

* First version: firmware v0.11, FUN60 Studio, ry-flash changes.

## -1. What?

Stop mechanical switches double typing on the FUN60 Ultra TMR, and get rid of MonsGeek's web
driver. Both meant replacing the firmware, so that's what this is.

## 0. How to flash

Linux, as written. Flashing goes through the keyboard's own bootloader over USB, so there's no
need to open the case. Everything below uses nix-shell; any Linux with the same tools works too.

The command line below is the recommended way. There's also an **experimental** flasher in the
web driver (https://driver.vivlos.dev, Firmware page) with a version list marked stable or
experimental. It sends the same bytes as ry-flash, checked frame by frame, and it has flashed my
keyboard a few times now. Its Download links give the same files for ry-flash.

**0.1 Build.**

```sh
# firmware -> fw/build/fun60_dfu.bin (fetches Artery's BSP into fw/vendor the first time)
nix-shell -p gcc-arm-embedded gnumake git --run 'make -C fw vendor && make -C fw'
nix-shell -p gcc --run 'make -C fw test'          # host tests

# flasher
cd flasher && nix-shell -p cargo rustc pkg-config udev openssl --run 'cargo build --release' && cd ..
```

A flasher built inside nix-shell only runs where Nix is installed. On another distro, build it
with that distro's packages (on Ubuntu: `cargo pkg-config libudev-dev libssl-dev`).

**0.2 Flash.** The keyboard can be on stock or on this firmware.

```sh
sudo flasher/target/release/ry-flash --image fw/build/fun60_dfu.bin --custom --arm
```

**0.3 Back to stock.** This downloads v306 from MonsGeek's server and flashes it:

```sh
sudo flasher/target/release/ry-flash --auto --stock-dir stock --arm
```

**0.4 If it goes wrong.**

- Hold **Esc** while plugging the cable in: the keyboard goes straight to its bootloader
  (`lsusb` shows `3151:502a`), then flash again. This needs a working mechanical switch on Esc.
- A build that hangs, or never talks to the PC in its first 10 s, drops itself into the
  bootloader. The watchdog handles hangs; a "has a host ever answered" check handles the rest.
- A keyboard stuck as `3151:502a` is not bricked. That's the bootloader, and any successful flash
  brings it back. Run the same flash command again.
- If all of that fails, you open the case and bridge one pad: [section 5](#5-recovery-with-the-case-open).
  I've had to do it once.

**0.5 Virtual machines.** Flash from Linux on real hardware if you can. When the keyboard switches
to its bootloader, it disconnects and comes back as a different USB device (`3151:502a`), and at
full speed instead of high speed. A VM's USB passthrough usually doesn't follow it, so ry-flash
gives up with "did not re-enumerate to bootloader (502A) within 20s", and the keyboard sits in the
bootloader on the host. In the one report so far (VirtualBox with its USB 2.0 controller),
connecting it to the VM by hand put it on the emulated USB 1.1 controller, where the flash
commands timed out (`hidapi error: ioctl (SFEATURE): Timer expired`). A Linux live USB on the
same PC works. In VirtualBox, switching the VM to the USB 3.0 (xHCI) controller and adding USB
filters for both `3151:5030` and `3151:502a` might work, but nobody has tested it.

## Contents

1. [The board](#1-the-board)
2. [Status](#2-status)
3. [The double typing](#3-the-double-typing)
4. [The brick](#4-the-brick)
5. [Recovery with the case open](#5-recovery-with-the-case-open)
6. [The old firmware tree](#6-the-old-firmware-tree)
7. [Writing new firmware](#7-writing-new-firmware)
8. [Battery, radio, lights](#8-battery-radio-lights)
9. [Keys](#9-keys)
10. [Web driver](#10-web-driver)
11. [Tools](#11-tools)
12. [Hardware map](#12-hardware-map)
13. [Corrections and open questions](#13-corrections-and-open-questions)
14. [Credits](#14-credits)
15. [License](#15-license)

