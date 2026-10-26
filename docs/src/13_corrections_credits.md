
## 13. Corrections and open questions

### 13.1 Corrections

Things that were believed, written down or acted on, and turned out wrong.

| Claim | What's true | How it was found |
| :--- | :--- | :--- |
| The upstream `firmware/` tree runs on this board | It used the wrong USB controller and bricked it (section 4) | the brick |
| A scan of the stock binary proves it uses high-speed USB | The scan proved nothing; the kernel log did | re-reading the scan |
| The double typing is a bad switch | Every switch does it on hard taps; stock reports contact bounce as presses | swapping switches, then the logs |
| PC13 is the Caps Lock switch | PC13 is USB power; the switch only changes PB1 and cuts the battery in the middle | measuring all three positions |
| The Fn layer is broken in firmware | The Fn switch was dead | `rawscan.py --keys` |
| The tester's lockout formula | Wrong for bursts of bounces; replaced by replaying logs through real filters | replays |

### 13.2 Open questions

- Bluetooth: built, never tested.
- Magnetic switches: the travel model rests on two switches (Space and Fn). Other switch models
  could rest outside 1000 to 3300 counts or travel less than 400, and then they never type.
- Sleep: stock's timed deep sleep is mapped (12.7), but the ERTC clock and prescaler, and whether
  the watchdog runs in deep sleep, are not known yet.
- Radio state values other than 3 and 4, and frames 0x95 to 0x98 (12.8).
- Why the bootloader stopped answering feature reports inside VirtualBox (0.5). Likely the
  emulated USB 1.1 controller, not proven.

## 14. Credits

- **[dot-agi/ry5088-flasher](https://github.com/dot-agi/ry5088-flasher)** by Pratyush Shukla (MIT):
  `ry-flash`, the USB flasher everything here is flashed with. It's in `flasher/` with a few
  changes, listed in its README. Its `firmware/` tree is not used; that's the one that bricked the
  board (section 4).
- **Artery Technology**: the AT32F402/405 BSP. `make vendor` fetches it, and four config files
  copied into `fw/src` keep Artery's license header.
- **NSA's Ghidra**, for the decompile.
- **QMK**, whose debounce docs name the algorithm used here.
- **[Sayobot's sayo-device-web-hid](https://github.com/Sayobot/sayo-device-web-hid)**, for how a
  WebHID config tool is put together. Wooting's Wootility, for the look to aim for. No code from
  either.
- **MonsGeek / RongYuan**: the stock firmware that was analysed. It isn't included here; ry-flash
  downloads it from their server when you want it back.
- **Claude Opus 5.5** (Anthropic, in Claude Code, medium and xHigh reasoning) did the reverse
  engineering and wrote the firmware, driver, tools and docs. I did the hardware side, the
  testing, and kept telling it when it was wrong.
- **Gemini 3.8 / 3.7 Flash (High)** flashed the build that bricked the keyboard, and is the reason
  the case had to come apart. See section 4.

## 15. License

MIT for everything here except where a file says otherwise. `flasher/` is MIT, Pratyush Shukla
(`flasher/LICENSE`). The Artery files keep Artery's license.
