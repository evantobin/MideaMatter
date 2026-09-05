# Build a Matter controller for Midea-UART mini-splits

This is a direct, local controller for **Midea-UART-compatible** mini-splits.
An ESP32-C6 replaces a compatible Smart Kit / Wi-Fi dongle, speaks the indoor
unit's Midea UART protocol, and exposes a standard Matter thermostat. The
Senville SENA/18HF with its `SEN20-ACK1T`-style Smart Kit port is the reference
build for this guide; other compatible Midea-built and rebranded units are
covered in [Which mini-splits are supported](#which-mini-splits-are-supported).

No Home Assistant. No cloud account. No MQTT. No Wi-Fi password in source code.

I wrote this as the guide I wanted before opening the indoor unit.

**Jump to:** [Shopping list](#shopping-list) | [Wiring](#wiring) |
[Compatibility](#which-mini-splits-are-supported) |
[Build and flash](#build-and-flash) | [Matter pairing](#first-boot-and-matter-pairing) |
[Troubleshooting](#troubleshooting)

## What it does

Your Matter controller gets current temperature, target temperature, and Off /
Auto / Cool / Heat / Dry / Fan Only controls. The same Room Air Conditioner
endpoint also exposes fan speed and vertical/horizontal swing. State changes
from the handheld remote are reported back through Matter.

The Midea UART protocol has no dependable compressor-running signal. Heat or
Cool therefore reports as active when selected; Auto, Off, Dry, and Fan-only
appear idle.

## Which mini-splits are supported

This project is for **Midea-UART-compatible indoor units**: units with the
low-voltage Smart Kit / Wi-Fi-dongle port that carries the Midea air-conditioner
protocol at **9600 8N1**. It is not a universal mini-split controller.

The intended, first-use target is the **Senville SENA/18HF** family. The bundled
[MideaUART package's compatibility list](libraries/MideaUART/README.md) is
explicitly incomplete, but names Midea, Electrolux, Qlima, Artel, Carrier,
Comfee, Inventor, and Dimstal/Simando as supported brands. Many of those brands
sell Midea-built units under their own names, so the **indoor unit electronics
and Smart Kit port matter more than the badge**.

| Situation | Expectation |
| --- | --- |
| Midea-built indoor unit with the same UART Smart Kit dongle/port | Likely compatible; verify wiring and test safely. |
| A rebranded unit from one of the package's listed brands | Plausibly compatible, but not a guarantee for every model or production year. |
| A port that is actual USB, a wired-wall-thermostat bus, or another proprietary connector | Not supported by this wiring or firmware. Do not connect it until you have confirmed it is the Midea UART Smart Kit port. |

Mode, fan-speed, and swing hardware vary by indoor unit. Matter exposes the
standard controls, but an option your particular head lacks may be ignored or
reported differently by the unit. Start with Off, Cool, Heat, and setpoint
changes; then try fan speed and each swing direction one at a time.

## The idea

```text
┌──────────────┐      Matter over Wi-Fi       ┌──────────────┐  5 V UART   ┌──────────────┐
│   Matter     │ ◀──────────────────────────▶ │   ESP32-C6   │ ◀─────────▶ │ Midea IDU    │
│  controller  │     BLE only while pairing   │ ESP-Matter   │             │ Smart Kit    │
└──────────────┘                              └──────┬───────┘             └──────────────┘
                                                       │ 3.3 V ↔ 5 V
                                                ┌──────▼───────┐
                                                │  TXS0108E    │
                                                └──────────────┘
```

The Smart Kit jack looks like USB-A, but is not USB. It carries a 5 V UART link
at 9600 baud. A Matter controller commissions the C6 over Bluetooth Low
Energy, then passes it Wi-Fi credentials. Those credentials are stored in C6
flash and are never placed in the firmware source.

## Shopping list

These are typical US Amazon-style prices for one controller as of August 2026;
listings and multipack sizes change often. The per-build column is the useful
number if a kit leaves parts over.

| Part | Buy / use | Typical purchase | Per build |
| --- | --- | ---: | ---: |
| [Seeed Studio XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html) | One board; 4 MB flash is sufficient. | $6–12 | $6–12 |
| [TXS0108E breakout](https://www.amazon.com/s?k=TXS0108E+bidirectional+logic+level+converter+breakout) | One 3.3 V ↔ 5 V UART level-shifter board. Multipacks are common. | $5–8 | $1–3 |
| Sacrificial USB-A male cable, **at least 3 ft** | Cut one end off, identify its conductors with a meter, and use it for the Smart Kit port. Most builders already have one. | Free | Free |
| [100–220 µF radial electrolytic capacitor](https://www.amazon.com/s?k=radial+electrolytic+capacitor+assortment+25v+100uf+220uf) | 16 V minimum; 25 V preferred. | $5–8 kit | $0.20–0.50 |
| [0.1 µF (100 nF) ceramic capacitor](https://www.amazon.com/s?k=ceramic+capacitor+assortment+50v+100nf) | 25 V minimum; 50 V preferred. | $4–7 kit | under $0.10 |
| [22 AWG stranded hookup wire and heat-shrink](https://www.amazon.com/s?k=22+awg+stranded+hookup+wire+heat+shrink) | Short leads make the UART happier. | $5–10 | $1–2 |
| [Multimeter](https://www.amazon.com/s?k=digital+multimeter) | Required to verify the Smart Kit 5 V and pin orientation. | $12–25 | — |

Expect roughly **$14–28 in controller parts**, excluding tools and an enclosure.
The capacitor kits cost more up front, but the rest of the parts are useful for
later builds.

The TXS0108E handles the 3.3 V ↔ 5 V UART connection between the XIAO and the
indoor unit.

## Safety first

Disconnect power to the indoor unit before opening it. Work only on the
low-voltage Smart Kit connector; do not touch mains wiring or outdoor-unit
terminals.

## Wiring

On the XIAO ESP32-C6, the UART uses **D6 / GPIO16** for TX and **D7 / GPIO17**
for RX.

```text
Indoor Smart Kit port                 TXS0108E                      ESP32-C6
─────────────────────                 ────────                      ─────────

 +5 V  ────────┬────────────────────▶ VCCB
               └───────────────────────────────────────────────────▶ XIAO 5V / VBUS
                                                                    │
                                                          100–220 µF + 0.1 µF
                                                                    │
 GND   ─────────────────────────────▶ GND ─────────────────────────▶ XIAO GND
 Unit TX ───────────────────────────▶ B2       A2 ─────────────────▶ D7 / GPIO17 (RX)
 Unit RX ◀─────────────────────────── B1       A1 ◀───────────────── D6 / GPIO16 (TX)

                                         VCCA ◀───────────────────── XIAO 3V3
                                         OE   ◀───────────────────── XIAO 3V3

The two capacitors connect in parallel between XIAO 5V / VBUS and GND.
```

The serial signals cross: **unit TX goes to ESP RX**, and **ESP TX goes to
unit RX**. Smart Kit `+5 V` powers both the TXS0108E 5 V side and the XIAO's
`5V` / `VBUS` input. Never connect it to the XIAO `3V3` pin. Do not also plug
USB-C power into the XIAO unless the two supplies are isolated.

Before powering up, meter the Smart Kit port's 5 V and GND. Many Midea ports
use USB-style data positions for RX/TX, but verify against the original dongle
or indoor-board markings rather than trusting wire colors.

## Software layout

```text
mideamatter/
├── main/                      # Matter thermostat and Midea UART bridge
├── components/                # ESP-IDF component wrappers
├── CMakeLists.txt             # native ESP-IDF / ESP-Matter project root
├── sdkconfig.defaults         # ESP32-C6 and Matter configuration
├── README.md                  # this guide
└── libraries/
    └── MideaUART/             # bundled Midea protocol library
```

The firmware uses native ESP-IDF and ESP-Matter plus the bundled MideaUART
protocol implementation. It compiles MideaUART directly for the indoor-unit
frame protocol, polling, status parsing, and control commands; this project
does **not** reimplement that protocol. The code in `main/hvac/` is the
ESP32-C6 UART and state bridge, while `main/matter/` maps that state to Matter
thermostat, fan-speed, and swing controls.

There is no Arduino runtime, HomeSpan, Home Assistant, or cloud dependency.

## Build and flash

Install Espressif's ESP-IDF 6.x and a compatible checkout of ESP-Matter. From
the project root, load the supplied environment helper:

```sh
cd mideamatter
. ./env.sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

`env.sh` defaults to ESP-IDF at `~/.espressif/v6.0.2/esp-idf` and ESP-Matter
at `~/esp/esp-matter`. If yours are elsewhere, set `ESP_IDF_EXPORT` and/or
`ESP_MATTER_PATH` before sourcing it.

The supplied partition table reserves a 3.8 MB application partition, so use a
4 MB XIAO ESP32-C6. The build has no Arduino or PlatformIO step.

## First boot and Matter pairing

1. Open the ESP-IDF monitor at **115200 baud** immediately after flashing.
2. If the device is uncommissioned, the firmware prints a manual pairing code
   and a QR-code URL.
3. In your Matter controller, choose its **Add Accessory** flow, then enter the
   code or scan a QR code made from that URL.
4. The controller supplies Wi-Fi credentials over BLE.
5. Rename the thermostat and assign it to the right room.

For a new home or Wi-Fi network during development, run `idf.py erase-flash`,
flash again, then use the new pairing code shown in the monitor.

## Bring-up checklist

1. Flash and pair the ESP32-C6 before connecting it to the mini-split.
2. Confirm it stays powered and visible in your Matter controller.
3. With the mini-split unplugged, wire the level shifter and inspect every lead.
4. Reconnect the mini-split, then connect ESP ground and UART wires.
5. Wait a few seconds for MideaUART to discover the indoor unit.
6. Test Cool, Heat, Off, and one setpoint change at a time.
7. Test the handheld remote and check that the Matter controller follows it.

## Pin and protocol settings

```cpp
constexpr int HVAC_UART_RX_PIN = 17; // XIAO D7
constexpr int HVAC_UART_TX_PIN = 16; // XIAO D6
constexpr uint32_t HVAC_UART_BAUD = 9600;
```

The mini-split target range is 17–30 °C. Your Matter controller may display
that range in Fahrenheit, depending on its unit preference.

## Browser log page

After the controller has joined Wi-Fi, open `http://<controller-IP>/` from a
device on the same local network to view a rolling live log. This avoids
connecting computer USB power while the Smart Kit port powers the XIAO. The log
is kept only in RAM and is cleared on reboot. Do not expose this page to the
internet; it is intended only for local troubleshooting.

## Matter Auto is an approximation

A Matter thermostat has separate heat and cool thresholds in Auto mode. The
Midea indoor unit has one Auto target. This firmware maps Midea's target to the
middle of Matter's required 2.5 °C comfort band:

```text
Matter Auto:   heat ───── 2.5 °C ───── cool
                              │
Midea Auto:                 target
```

Changing either Matter Auto threshold sends the midpoint to the mini-split.
Heat and Cool are direct one-target mappings.

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Firmware is too large | Confirm the XIAO has 4 MB flash and use the supplied partition table. |
| Board resets | Check the Smart Kit 5 V rail and capacitor polarity; use a separate supply only if that rail sags. |
| Pairs but has no unit data | Re-check shared ground, crossed TX/RX, `VCCA`, `VCCB`, and `OE`. |
| Unit ignores commands | Verify Smart Kit pin orientation. |
| Serial link unstable | Keep UART wires short and re-check the TXS0108E power and `OE` wiring. |
| Matter controller says “No Response” | Check Wi-Fi and that its hub/controller is online. |
| No pairing code | Run `idf.py erase-flash`, flash again, and reopen the monitor at 115200. |

## Known limitations

- Matter controllers decide how to present fan speed, swing, Dry, and Fan Only.
  The endpoint reports standard Matter clusters, but an individual controller
  may choose not to put every control on its main thermostat screen.
- This uses the indoor-unit temperature sensor, not a remote room sensor.
- This single-app partition has no OTA application slot; update by USB.
