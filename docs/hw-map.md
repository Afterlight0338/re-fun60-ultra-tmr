# FUN60 Ultra TMR (dev_id 2307) hardware map

Derived from stock firmware v306 (`stock/2307/fw_at32.bin`, SHA-256 matches the vendor manifest)
by decompiling it with Ghidra 12 and reading the result. Every row names the stock function it
comes from, so any of it can be re-checked. Nothing here comes from the older `firmware/` tree
or its docs. Confirmed on the board itself where marked.

## MCU and clocks

| Item | Value | Evidence |
|---|---|---|
| MCU | AT32F405RCT7 (LQFP64), silkscreen "RY5088" | board photo |
| Crystal | 12.000 MHz (Y1) | board photo; `crm_pll_config(HEXT, 72, 1, FP_4)` in `system_clock_config` @0x08016650 |
| SYSCLK / PLLU | 216 MHz / 48 MHz (`crm_pllu_div_set(5)`) | @0x08016650 |
| USB | OTGHS1 on-chip PHY, high speed | stock enumerates high-speed (kernel log 2026-09-27) |
| App | vector table 0x08005200, VTOR set by `main` @0x08014b90 (`nvic_vector_table_set(0x08000000, 0x5200)`) | main |
| Delays | SysTick: `FUN_08013b40(n)` = n us, `FUN_08013ae4(n)` = n ms | decompile |

## Board init (`FUN_080167b8`, called from main)

All pins are first set to analog by `FUN_08013078`. Then:

| Pin | Config | Initial | Role (evidence) |
|---|---|---|---|
| PB1 | input, pull-up | read into config bits 1-2 | **mode switch at the top (Mac) when low**: measured 2026-09-30, low at the top, high in the middle and at the bottom. `FUN_08014e70` polls it and after 15 steady reads flips the same bits a key action flips (`FUN_0800cfb0` case 10), saves config and reloads the keymaps from `bits * 0x400` (a Mac layout) |
| PC13 | input, pull-up | | **USB power present when high**: measured high in all three switch positions with the cable in. Stock boots in wired mode on it (`FUN_08014ef4`), only allows Fn + U with it high (key handler case 0xe / 6), and runs the battery rules above on it. The switch under Caps Lock (top = wireless Mac, middle = wired with the battery disconnected, bottom = wireless Windows, per MonsGeek's FUN60 guide) is not read for the mode: its middle position just cuts the battery (the battery reading rises slightly there, 1509 vs 1488 counts, as the load goes away) |
| PA3 | input, pull-up | | charger IC status, low = charging: with PC13 high, low caps the level at 99 % and lights LED 0 red (`FUN_080148dc`) |
| PB13 | output | high | LED power: pulled low after the last LED DMA frame before sleep (`FUN_080154b4`), set high again before the LEDs are re-initialised (`FUN_08014f38`) |
| PC4 | output | high | ? : in the sleep loop, high while the battery is under 41 %, low above (`FUN_080154b4`) |
| PC8 | output | high | column shift-register data |
| PC9 | output | high | column shift-register clock |
| PA8 | output | high | column output enable, active low |
| PB7, PB8, PB9 | output | | row mux address A2, A1, A0 |
| PC14 | output | high first | power enable 1 (off during timed deep sleep) |
| PC5 | output | high 20 ms after PC14 | power enable 2 (stays on in sleep) |
| PA7 | output | high 100 ms after PC5 | power enable 3 (off during timed deep sleep) |

The PC14, PC5, PA7 sequence with delays looks like rail sequencing through the three
transistors next to the regulator (Q2 to Q4 on the PCB). Without it every sensor reads 0 V
(confirmed on the board: all 84 sites read 0 to 2 counts under the old firmware, which drove
none of these).

## Key scan (`keyscan_column` @0x0800577c, one column per call from the main loop)

ADC (`FUN_08012d60`): ADC1 channel 2 on **PA2** (analog), clock HCLK/8 = 27 MHz, sample time
28.5 cycles, single conversion, **software trigger enabled** (`OCTESEL = 111`, `OCTEN = 1`),
calibrated, then 1000 warm-up conversions. **PF7** is an output left low: the mux enable
(74HC4051-style INH, active low).

Per column `c` (0..13):
1. PC8 high, PA8 high, mux address 000.
2. `c + 1` times: PC9 high, PC8 low, PC9 low. (Walks a single 1 to output `c`.)
3. PA8 low (outputs on), wait 5 us.
4. For row `r` = 1..5 (row 0 is never scanned; sites flagged in a table at RAM +0x1822 are
   skipped): select mux channel `r - 1`, except column 6 row 5 which uses channel 6
   (`row_mux_select` @0x0801207c: A0 = PB9, A1 = PB8, A2 = PB7). Wait 1 us, convert.
   Stock keeps an 8-sample ring per site and averages it.
5. `c + 1`; after 13 wrap to 0. Then PA8 high, mux 000, and `14 - (c + 1)` more clock pulses
   (14 on wrap) to shift the 1 out of the chain.

So the matrix is **14 columns x 5 rows = 70 sites**, not 84.

## Other peripherals stock enables

GPIOA-D, GPIOF, PWC, DMA1, ADC1, SPI2, SPI3, TMR4, TMR6, OTGHS. SPI3 on PB3/PB4/PB5 (plus
PA15, PD2) goes to the PAN1080 radio (`FUN_08016344`); SPI2 + DMA1 drive the LEDs (below); TMR6
is stock's tick (its interrupt also restarts LED frames).

## LEDs (per-key RGB, WS2812-class)

| Item | Value | Evidence |
|---|---|---|
| Count | 61, one per key (1464-byte frame = 61 x 24) | `FUN_080158d0` fills two 0x5B8 buffers |
| Data pin | PA10, alternate function 5 (SPI2 MOSI), push-pull, pull-up, moderate drive | `FUN_080158d0` |
| SPI2 | half-duplex TX, master, HCLK/2/16 = 6.75 MHz (1.19 us per LED bit), MSB first, 8-bit, CPOL low, CPHA 2nd edge, software CS; CTRL2 `dmaten` + `hwcsoe`, then `spien` | `FUN_080158d0` |
| DMA | DMA1 channel 1, DMAMUX request 0x0D (SPI2_TX), memory -> SPI2 DT (0x4000380C), byte to byte, memory increment, no loop, priority high, 1464 bytes | `FUN_08013a34`; stock `dma_init` @0x08013bd8 |
| Encoding | 24 SPI bytes per LED in G, R, B order, MSB first; `0xF0` = 1 bit, `0xC0` = 0 bit; value scaled by brightness (v * br / 150) | pixel writer @0x08007320 |
| Frames | drawn into a back buffer, copied to the DMA buffer in the main loop; the TMR6 interrupt (vector 70 @0x08011ea8) re-runs the DMA setup whenever channel 1's transfer-complete flag is set | main, `app_vec70` |

**Chain order** (from stock's indicator code: mode keys E R T Y U light LEDs 24 23 22 21 20
via the table at 0x0801a238; Caps is 28, Win 54): a snake. Esc = 0 .. Backspace = 13, then
`\` = 14 back to Tab = 27, Caps = 28 .. Enter = 40, RShift = 41 back to LShift = 52,
LCtrl = 53 .. RCtrl = 60.

**Indicators** (drawn over the effect with `FUN_08007320(led, 100, r, g, b)`):

| What | Where | Stock function |
|---|---|---|
| Mode: Bluetooth blue, 2.4G green, wired red (RAM table 0x20008162, compressed initial data) | E R T / Y / U | `FUN_08016910`, state from `FUN_08015764` |
| Solid 2 s: switched to wired, or connected (radio state 3) | mode key | `FUN_0801587c(1)` |
| Fast blink, 0.1 s: pairing (radio state 4) | mode key | `FUN_0801587c(2)` |
| Slow blink, 0.9 s: wireless, not connected (states 0-2) | mode key | `FUN_0801587c(3)` |
| 2.4G pairing: everything dark, Esc white | all | `FUN_08014b24` |
| Battery on Fn + Space (type 0x0a arg 8): < 35 % red, < 55 blue, < 75 magenta, < 95 yellow, else green | Esc | `FUN_080148dc` |
| Charging on USB power: red; on battery under 21 %: red blink, 0.4 s | Esc | `FUN_080148dc` |
| Caps Lock on | Caps, white | `FUN_08016910` |
| Win lock on | Win, white | `FUN_08016910` |

Stock's initialised RAM (0x20000000, 0x849c bytes) is stored compressed at 0x0801a640 (Keil
`__scatterload` table at 0x0801a620, decompressor at 0x0800541c). The mode colours and the
pairing hold time (3 s) come from decompressing it with a transcription of that routine.

Stock's default keymap is a table at 0x184B8 (column-major, 6 rows x 4 bytes per key: type,
arg, code lo, code hi); its Fn layer is at 0x188B8. Types seen: 0x00 keyboard usage, 0x03
consumer usage (16-bit), 0x0A layer key (Fn), 0x0D lighting controls, 0x0E / 0x08 wireless and
profile functions.

## Battery and charging (`FUN_080151f8`, every 200 ticks)

| Item | Value | Evidence |
|---|---|---|
| Battery voltage | divider on **mux channel 7**, same ADC (ADC1 ch2 on PA2); any column | `row_mux_select(7)`, wait 5 us, one conversion |
| Filtering | 8-sample ring, averaged, then averaged again with the previous result | same function |
| Level curve | >= 1595 counts: 100 %; < 1201: 1 %; 1201..1320: 1 to 20 %; 1321..1594: 20 + (v - 1320) * 80 / 275 % | same function |
| Level rules | on battery (PC13 low) the level only falls, after 10 lower readings in a row; on USB power it only rises, capped at 99 % while PA3 says charging | same function |
| Low battery | level under 6 % on battery: flags a power-down (`FUN_0801587c`) | same function |
| Wired mode | stock skips all of this in connection mode 6 | first test in the function |
| Battery unplugged | channel 7 reads about 69 counts (our scan, 2026-09-28) | rawscan |

Charging is done by a charger IC; firmware only reads its status pin (PA3), so no firmware can
overcharge the cell. Our firmware reads the same way from v0.06 (`fw/src/power.c`).

## Sleep (stock)

`FUN_080154b4` is the sleep loop, used only outside wired mode: wait for SPI3 idle, finish the
LED frame, PB13 low (LED power off), `FUN_08016344(1)` (radio pins to their sleep setup), PA8
high (column outputs off), then loop: scan keys with `keyscan_column` until one wakes it; on a
timer flag, PC14 and PA7 low, **timed deep sleep** `FUN_08014cd4(25)`, rails back on.

`FUN_08014cd4(n)` is a timed deep sleep on the ERTC wakeup timer: stop the wakeup timer, wait
for `WATWF`, load `n - 1` into `ERTC_WAT`, enable it, stop SysTick, switch SYSCLK to HICK,
lower the LDO (`PWC_LDOOV`), set the regulator to low power (`PWC_CTRL` bit 0), `SLEEPDEEP` + WFI,
then restore SysTick, LDO, HEXT, the PLL and SYSCLK.

Not yet known: the ERTC clock and prescaler (so the real sleep length), and whether the
watchdog runs in deep sleep on this chip (user system data bit `depslp_wdt`).

## Radio link (PAN1080)

The PAN1080 runs its own firmware (never touched by us) and keeps its own pairings. The MCU
only tells it which mode to be in and hands it HID reports, over SPI3.

| Item | Value | Evidence |
|---|---|---|
| Bus | SPI3 on PB3 (SCK), PB4 (MISO), PB5 (MOSI), AF6; master, full duplex, 8-bit, MSB first, CPOL low, CPHA 2nd edge, APB1/16 = 6.75 MHz, software CS | `FUN_08016344` |
| Chip select | PA15, output, idle high; low for each transfer; one 300 us low pulse at init | `FUN_08016344`, `FUN_08015c80`, `app_vec28` |
| Radio wants attention | PD2, input with pull-up, low = a reply is waiting | `FUN_08016344`, `FUN_08015c80` |
| DMA (stock) | TX DMA1 ch2 (DMAMUX 0x0F), RX DMA1 ch3 (0x0E); transfer done interrupt raises CS | `FUN_08015c80`, `FUN_08016500`, `app_vec28` |
| Pacing | TMR6 = 125 us tick, TMR4 = 5 ms tick. Reports need 2 TMR6 ticks since the last transfer (4 for mouse), control frames 2 TMR4 ticks, polls 8 TMR6 ticks | `FUN_08016b1c` (TMR6 125 / 215), TMR4 5000 / 215, `app_vec46`, `FUN_08015c80` |
| Connection mode | byte 0x2000849c + 0x1a: 0, 1, 2 = Bluetooth 1-3, 5 = 2.4G, 6 = wired | case 0xe of the key handler, `FUN_0800cfb0` |

Frames, both directions: `[cmd][n][n payload bytes][sum of payload]`, sent as one transfer
rounded up to a multiple of 4 bytes. What stock sends, in priority order (`FUN_08015c80`):

| Cmd | Payload | When |
|---|---|---|
| 0x09 | 64 zero bytes (68-byte transfer) | PD2 low: fetch the radio's reply, which comes back in the same transfer |
| 0x91 | 64-byte vendor reply | driver traffic over the air |
| 0x93 | mode | at boot and after a mode change; then asks for status |
| 0x92 | one byte (0 in practice) | repeated every control slot until a status reply arrives |
| 0x94 | op: 1 connect, 4 (in state 4), 2 = Bluetooth pairing followed by name length 13 and "FUN60 Ultra-N", padded to 33 bytes | pairing keys, and on status while a 2.4G pairing is pending |
| 0x95 to 0x98 | one or two bytes | not mapped yet |
| 0x81 | `[type][data]`: 0 mouse (7 bytes), 1 keyboard (mods + 6 keys), 2 NKRO bitmap (15 bytes), 3 consumer (LE16), 4 one byte, 5 31 bytes, 6 vendor (64) | report flags in 0x2000801c; wireless modes only |
| 0x90 | battery % | level changed, on USB power or low battery |

Replies (`app_vec28`, checked with the same sum): payload type 0x10 is status
`[0x10][flags: host LEDs in bits 0-2][state][mode]`; if the mode differs from the MCU's, the MCU
sends 0x93 again. 0x00 and 0x11 carry 64 bytes of host data (the driver over the air). 0x12 stops
the status requests. 0x66 or 0xFF ask for status again.

Mode keys (Fn layer, type 0x0e): E R T = Bluetooth 1-3, Y = 2.4G, U = wired; press switches and
saves the mode; holding a wireless one (per-key hold time, table at 0x20008060) pairs:
Bluetooth sends the 0x94 name frame, 2.4G sets a flag that sends 0x94 op 1 on every status reply
until the state is 5.

Our firmware reproduces this from v0.07 (`fw/src/radio.c`), with polled SPI instead of DMA and
1 ms / 10 ms pacing. **Confirmed on the board 2026-09-30: 2.4G typing through the stock dongle**
(v0.11, the dongle paired under stock reconnected without pairing again). Not yet confirmed: the
meaning of the state values other than 3 and 4 (read from stock), and Bluetooth.
