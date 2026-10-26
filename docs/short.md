## What this is

Replacement firmware for the MonsGeek FUN60 Ultra TMR, plus a web driver to replace MonsGeek's.
It started because mechanical switches in this board type a letter twice on hard taps, and
MonsGeek's debounce setting does nothing about it. Stock reports every contact bounce as a new
press. This firmware reports a press at once and holds the release back for 8 ms: 0 doubles in
121 hard taps, where stock did 13 to 165 per run.

This is the short version: is it your keyboard, what to run, how to get back. The long version
has the brick, the recovery, the wrong turns and every pin.

## Is this your keyboard?

| Check | Must be |
| :--- | :--- |
| Keyboard | MonsGeek FUN60 Ultra **TMR**, board ID 2307 |
| USB ID | `3151:5030` when running |
| Stock firmware | v306 |
| Switches | **mechanical**. Magnetic support is new in v0.12 and untested |

Tested on exactly one keyboard.

## What works, what doesn't

| Works | Not yet |
| :--- | :--- |
| USB typing, Fn layer, media, arrows | **sleep**: on battery the chip runs flat out |
| per-key RGB and stock's indicator lights | Bluetooth (built, never tested) |
| 2.4G through the stock dongle | the web driver over 2.4G |
| battery level and charging | Mac mode, NKRO (6 keys at once) |
| settings saved on the keyboard | rapid trigger, per-key actuation, calibration |
| FUN60 Studio web driver | the MonsGeek app (never planned) |

## What you need

* Linux on real hardware (not a VM, see below) and a USB cable
* the GitHub repo, built:

```
nix-shell -p gcc-arm-embedded gnumake git --run 'make -C fw vendor && make -C fw'
cd flasher && nix-shell -p cargo rustc pkg-config udev openssl --run 'cargo build --release' && cd ..
```

Not on NixOS? Build the flasher with your distro's packages (Ubuntu:
`cargo pkg-config libudev-dev libssl-dev`). A flasher built inside nix-shell won't run without Nix.

## Flash it

```
sudo flasher/target/release/ry-flash --image fw/build/fun60_dfu.bin --custom --arm
```

The keyboard can be on stock or on this firmware. Your MonsGeek settings get wiped.

## Go back to stock

```
sudo flasher/target/release/ry-flash --auto --stock-dir stock --arm
```

This downloads v306 from MonsGeek's server and flashes it.

## If it gets stuck

* **Shows up as `3151:502a`?** That's the bootloader. Not bricked. Run the flash command again.
* **Want the bootloader on purpose?** Hold Esc while plugging the cable in.
* **Firmware hangs?** It drops itself into the bootloader after 10 s.
* **Nothing at all on USB?** Open the case and bridge one pad. That's section 5 of the long version.

## Don't flash from a VM

When the keyboard enters its bootloader, it comes back as a different USB device at a different
speed. VM passthrough doesn't follow it, so the flash times out and the keyboard waits in the
bootloader. Use a Linux live USB on the same PC.
