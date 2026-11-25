# XInput → DualShock 4 Bridge

![Platform](https://img.shields.io/badge/platform-RP2040-blue)
![Language](https://img.shields.io/badge/language-C11-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

**Use a Bluetooth Xbox controller on a PlayStation 4. No PC, no software, two Raspberry Pi Picos.**

A Pico W receives the gamepad over Bluetooth and re-emits its buttons and sticks as GPIO levels
and PWM voltages. A plain Pico running GP2040-CE reads those lines and enumerates over USB as a
DualShock 4. The console just sees a wired DS4.

🇷🇺 [Русская версия](README.ru.md)


---

## Contents

- [Why](#why)
- [How it works](#how-it-works)
- [Hardware](#hardware)
- [Why these values](#why-these-values)
- [Build and flash](#build-and-flash)
- [GP2040-CE setup](#gp2040-ce-setup)
- [Pairing a controller](#pairing-a-controller)
- [Debugging](#debugging)
- [Latency](#latency)
- [Limitations](#limitations)
- [Roadmap](#roadmap)
- [Repository layout](#repository-layout)
- [License](#license)

---

## Why

The PS4 only accepts authenticated DualShock 4 controllers over USB, so an Xbox pad cannot be
plugged in directly. Existing workarounds are either a PC running Remote Play or a closed-source
commercial adapter.

This project does the entire translation in hardware for about $15 in parts. The signal path is
fully open: every line can be probed with a multimeter or a logic analyzer.

> **About the name.** XInput is the Windows USB API. Over Bluetooth an Xbox pad speaks plain HID,
> and that is what Bluepad32 actually parses. The repository name is kept for recognisability.

---

## How it works

```
                     Bluetooth (HID)
   ┌────────────────┐  classic + BLE  ┌────────────────────┐
   │ Xbox controller│ ──────────────► │ Pico W             │
   └────────────────┘                 │ Bluepad32 + BTstack│
                                      └─────────┬──────────┘
                                                │
                       18× GPIO (active LOW)    │
                       4× PWM 488 kHz → RC      │
                                                ▼
                                      ┌────────────────────┐
                                      │ Pico               │
                                      │ GP2040-CE (PS4)    │
                                      └─────────┬──────────┘
                                                │ USB HID (DS4)
                                                ▼
                                      ┌────────────────────┐
                                      │ PlayStation 4      │
                                      └────────────────────┘
```

| Stage | What is transferred | How |
|---|---|---|
| Controller → Pico W | HID reports | Bluetooth, BTstack + Bluepad32 |
| Pico W → Pico (buttons) | 18 lines, 3.3 V | direct wire, **LOW = pressed** |
| Pico W → Pico (sticks) | 4 voltages, 0…3.3 V | 8-bit PWM + RC filter → ADC |
| Pico → PS4 | DS4 report | USB HID, 1 kHz polling |

Both microcontrollers run at 3.3 V, so **no level shifting is needed on the digital path** —
only a common ground. All the analog conditioning is four RC filters that turn PWM into a
steady voltage.

### What the firmware does

- reads the gamepad through a custom Bluepad32 platform (`bridge_platform.c`);
- fans buttons out to 18 GPIOs and axes to 4 PWM channels;
- releases every button and re-centres the sticks when the link drops;
- holds a startup delay so GP2040-CE calibrates the axes against settled voltages.

---

## Hardware

### Bill of materials

| Ref | Qty | Part | Value | Purpose |
|---|---|---|---|---|
| U1 | 1 | Raspberry Pi Pico W | RP2040 + CYW43439 | Bluetooth receiver |
| U2 | 1 | Raspberry Pi Pico | RP2040 | GP2040-CE, USB device |
| R1–R4 | 4 | Resistor | 10 kΩ 1 % | RC filter |
| C1–C4 | 4 | Capacitor | 100 nF X7R | RC filter |
| D1–D2 | 2 | Schottky diode | BAT54 / 1N5817 | power rail isolation |
| R5–R22 | 18 | Resistor (optional) | 100 Ω | digital line protection |

Full BOM: [`hardware/bom.csv`](hardware/bom.csv) · Schematic: [`docs/hardware.md`](docs/hardware.md)

### Buttons: Pico W → GP2040-CE

All lines are active low: `LOW` = pressed, `HIGH` = released.

| GPIO | DS4 button | Bluepad32 constant |
|---|---|---|
| GP2 | Cross (✕) | `BUTTON_A` |
| GP3 | Circle (○) | `BUTTON_B` |
| GP4 | Square (□) | `BUTTON_X` |
| GP5 | Triangle (△) | `BUTTON_Y` |
| GP6 | L1 | `BUTTON_SHOULDER_L` |
| GP7 | R1 | `BUTTON_SHOULDER_R` |
| GP8 | L2 (digital) | `BUTTON_TRIGGER_L` |
| GP9 | R2 (digital) | `BUTTON_TRIGGER_R` |
| GP10 | L3 | `BUTTON_THUMB_L` |
| GP11 | R3 | `BUTTON_THUMB_R` |
| GP12 | Share / Select | `MISC_BUTTON_BACK` |
| GP13 | Options / Start | `MISC_BUTTON_HOME` |
| GP14 | PS / Guide | `MISC_BUTTON_SYSTEM` |
| GP15 | Touchpad click | `MISC_BUTTON_CAPTURE` |
| GP16 | D-Pad Up | `DPAD_UP` |
| GP17 | D-Pad Down | `DPAD_DOWN` |
| GP18 | D-Pad Left | `DPAD_LEFT` |
| GP19 | D-Pad Right | `DPAD_RIGHT` |

Face buttons are mapped **by physical position, not by label**: the bottom Xbox button (A)
becomes Cross, the right one (B) becomes Circle, and so on. Muscle memory carries over.

### Sticks: PWM → RC → ADC

| Pico W GPIO | PWM slice / channel | Filter | Pico pin | Axis | Field |
|---|---|---|---|---|---|
| GP21 | 2 / B | 10 kΩ + 100 nF | GP26 (ADC0) | Left X | `axis_x` |
| GP22 | 3 / A | 10 kΩ + 100 nF | GP27 (ADC1) | Left Y | `axis_y` |
| GP26 | 5 / A | 10 kΩ + 100 nF | GP28 (ADC2) | Right X | `axis_rx` |
| GP28 | 6 / A | 10 kΩ + 100 nF | GP29 (ADC3) | Right Y | `axis_ry` |

Every axis sits on **its own PWM slice**, so the four channels have independent duty cycles.
GP23, GP24, GP25 and GP29 on the Pico W are avoided — they are wired to the CYW43 radio.

### Power

Both boards share one 5 V rail through a Schottky diode-OR:

- PS4 USB port → VBUS of the Pico → diode → VSYS of the Pico W;
- VSYS sees roughly 4.7 V after the diode, inside the allowed 1.8…5.5 V range;
- no back-feeding, so either USB socket can power the whole adapter;
- total draw is under 100 mA, well within what the console port supplies.

No external supply is needed: **the adapter is powered entirely by the PS4's own USB port.**

### Layout notes

- **Put the capacitor right at the ADC pin.** It acts as the charge reservoir for the RP2040
  sample-and-hold, so the 10 kΩ source impedance causes no sampling error.
- **Common ground is mandatory**, otherwise a floating reference ruins both digital and analog.
- **GP29 on an original Pico is tied to the VSYS divider.** It adds an offset and slightly
  compresses the right-stick Y range. GP2040-CE calibration absorbs it, but check the Pico
  schematic when laying out your own board.

---

## Why these values

**PWM frequency.** `clkdiv = 1.0`, `wrap = 255`, 125 MHz system clock:

```
f_pwm = 125 MHz / 256 = 488.28 kHz
```

**Filter.** R = 10 kΩ, C = 100 nF:

```
τ   = R·C = 1.0 ms
f_c = 1 / (2π·R·C) = 159 Hz
```

The carrier sits three thousand times above the corner frequency — roughly **−70 dB** of
attenuation.

**Ripple.** Worst case at 50 % duty:

```
V_ripple ≈ V·D·(1−D) / (f·R·C) = 3.3 · 0.25 / (488281 · 0.001) ≈ 1.7 mV p-p
```

That is about 2 LSB of the 12-bit ADC and **one eighth of a single PWM step**
(3.3 V / 255 = 12.9 mV). The filter does not eat into usable resolution.

**Settling time.** Reaching one DS4 step of accuracy (0.4 %) takes about 5.5 τ ≈ **5.5 ms**.
This is exactly why the firmware waits 3 seconds at startup: the capacitors must reach
mid-scale before GP2040-CE runs its auto-calibration.

**Why 8 bits is enough.** DS4 USB reports carry 8-bit stick axes as well. The home-made DAC
matches the resolution of the target protocol, so there is nothing to gain from 12 bits.

---

## Build and flash

### Prerequisites

- `arm-none-eabi-gcc`, `cmake` ≥ 3.13, `python3`
- [pico-sdk](https://github.com/raspberrypi/pico-sdk) — version pinned in `.gitmodules`
- [Bluepad32](https://github.com/ricardoquesada/bluepad32) — submodule under `third_party/`

### Build

```bash
git clone --recursive https://github.com/AV4KADA/xinput2ds4.git
cd xinput2ds4

export PICO_SDK_PATH=/path/to/pico-sdk

cmake -S firmware/bridge -B build -DPICO_BOARD=pico_w
cmake --build build -j4
```

> `CMakeLists.txt` must define both `ENABLE_BLE` and `ENABLE_CLASSIC`. Without them
> `btstack_config.h` deliberately stops the build with `#error`.

### Stack configuration

A few settings in `btstack_config.h` and `sdkconfig.h` are worth knowing about:

- **Classic and BLE are both on**, with cross-transport key derivation — modern Xbox pads
  pair over BLE while older ones use classic HID.
- **HCI controller-to-host flow control is enabled** and ACL buffers are capped at 3 to keep
  the CYW43 shared bus from overrunning.
- **Link keys live in flash** (TLV over the flash sector interface, 16 entries).
- **No malloc**: the ATT database is a fixed 512-byte block.

### Flash

1. Hold BOOTSEL on the Pico W and plug in USB.
2. Copy `build/bridge.uf2` to the `RPI-RP2` drive.

```bash
cp build/bridge.uf2 /media/$USER/RPI-RP2/
```

Prebuilt binaries are in [Releases](https://github.com/AV4KADA/xinput2ds4/releases).

---

## GP2040-CE setup

1. Flash the official GP2040-CE `.uf2` onto the **plain Pico**.
2. Hold the configured button at boot to enter web config, then open `http://192.168.7.1`.
3. **Settings → Input Mode:** set `PS4` as the default mode.
4. **Pin Mapping:** assign button inputs exactly as in the table above.
5. **Analog:** enable ADC0…ADC3 and run calibration. The bridge must be powered and the
   sticks must be released while calibrating.
6. Or simply import the ready-made config
   [`firmware/gp2040ce/board-config.bak`](firmware/gp2040ce/).

> **PS4 authentication.** GP2040-CE's PS4 mode needs authentication data from your own
> DualShock 4; without it the console drops the controller after roughly 8 minutes. Follow the
> official GP2040-CE documentation. **No keys or authentication files are stored in this repository.**

---

## Pairing a controller

1. Power the adapter.
2. The Pico W LED stays lit during init (about 3 seconds) and turns off once scanning starts.
3. Put the gamepad into pairing mode.
4. Input goes to the console as soon as it connects.

Keyboards are filtered out at discovery time by class of device (COD), so a random Bluetooth
peripheral cannot take a slot.

> Bonding keys are currently wiped on every boot, so the pad has to be re-paired after a power
> cycle. This is deliberate for now — see the [roadmap](#roadmap).

---

## Debugging

**Logs.** UART at 115200, level `Info`. The firmware prints only non-zero states so an idle
controller does not flood the channel:

```
btns=00000001 misc=00 dpad=00 LX=-12 LY=8 RX=0 RY=0
```

**Check the analog path with a multimeter**, probing the filter capacitor:

| Stick position | Expected voltage |
|---|---|
| released | ≈ 1.65 V |
| full left / down | ≈ 0 V |
| full right / up | ≈ 3.3 V |

**Test without a console.** Switch GP2040-CE to XInput mode, plug it into a PC and open any
gamepad tester — every button and axis is visible.

**A note on logging in release builds.** One log line is ~70 characters, which is about 6 ms
at 115200 baud. Disable logging (`CONFIG_BLUEPAD32_LOG_LEVEL 0`) for latency measurements and
for production firmware.

---

## Latency


| Stage | Value | Source |
|---|---|---|
| PWM carrier | 488.28 kHz | computed |
| Filter ripple | ≈ 1.7 mV p-p | computed |
| Axis settling to 1 DS4 step | ≈ 5.5 ms | computed |
| USB polling (GP2040-CE) | 1 ms | firmware setting |
| Bluetooth connection interval | TBD | measured |
| **End-to-end input lag** | **TBD** | measured |

The Bluetooth link and the RC filter dominate. Digital buttons bypass the filter, so button
presses land faster than stick movement.

---

## Limitations

An honest list of what is not perfect today:

- **PS4 authentication is required** for sessions longer than ~8 minutes.
- **L2 and R2 are digital.** The code uses the `BUTTON_TRIGGER_*` flags; the analog
  `brake` / `throttle` values are not used yet.
- **Axes do not reach the endpoints.** `128 + axis·127/512` produces a PWM range of 1…254
  instead of 0…255, losing about 0.8 % at each end. GP2040-CE calibration hides it.
- **Bonding keys are erased on every boot** (`uni_bt_del_keys_unsafe()`), so the pad must be
  paired again.
- **Only one gamepad should be active.** Bluepad32 is configured for 4 devices, but all of
  them would drive the same GPIOs.
- **Not forwarded:** rumble, gyro, accelerometer, touchpad coordinates.

---

## Roadmap

- [ ] Analog L2 / R2 via two more PWM channels
- [ ] Accept only the first connected gamepad, ignore the rest
- [ ] Persist bonding keys in flash, re-pair only on a held button
- [ ] Round-to-nearest axis mapping across the full 0…255 range
- [ ] Logic-analyzer latency report with published captures
- [ ] A single PCB carrying both RP2040s and all the passives

---

## Repository layout

```
firmware/bridge/     Pico W firmware: main.c, bridge_platform.c, BTstack configs
firmware/gp2040ce/   GP2040-CE version, settings, exported board config
hardware/            schematic and BOM
docs/                hardware notes, pinout, measurement method, photos
third_party/         Bluepad32 as a submodule
```

---

## License

Code — [MIT](LICENSE). Schematic and documentation — CC-BY-4.0.

A personal interoperability project. No Sony or Microsoft code is used or distributed.
PlayStation, DualShock and Xbox are trademarks of their respective owners.
