# Food Contamination Screening System

An inline optical screening prototype for food items on a conveyor. Each item is inspected with two complementary optical channels, **visible RGB reflectance** and **UV-induced fluorescence**, and a **Teensy 4.1** handles acquisition, classification, conveyor control and sorting.

> **Validation boundary:** This is an engineering screening prototype. It is **not** a validated food-safety detector. The current UV threshold in firmware is a **development placeholder** and must not be treated as a contamination limit. Any detection claim requires target-specific calibration and validation against an appropriate reference method.

---

## Table of Contents

1. [How it works](#how-it-works)
2. [Hardware](#hardware)
3. [Pin map](#pin-map)
4. [Firmware state machine](#firmware-state-machine)
5. [Signal processing and decision logic](#signal-processing-and-decision-logic)
6. [Calibration and riboflavin reference](#calibration-and-riboflavin-reference)
7. [Data logging](#data-logging)
8. [Fault handling](#fault-handling)
9. [UV safety](#uv-safety)
10. [Testing and validation plan](#testing-and-validation-plan)
11. [Roadmap](#roadmap)
12. [Known issues and things to verify](#known-issues-and-things-to-verify)
13. [Repository structure](#repository-structure)
14. [Getting started](#getting-started)
15. [References](#references)
16. [Team](#team)
17. [License](#license)

---

## How it works

Core principle:

```
SENSE → ACQUIRE → PROCESS → CLASSIFY → ACT → LOG
```

Operating sequence:

1. The conveyor moves the next item into the inspection area.
2. An **IR sensor** detects item arrival and triggers a cycle.
3. The controller waits for a stable measurement window.
4. **TCS34725** captures visible RGB + clear data with illumination ON and OFF.
5. The **UV ring + BPW34** photodiode capture fluorescence with UV ON and OFF.
6. Background is subtracted and readings are filtered.
7. Features are computed and the decision engine outputs **PASS**, **FLAGGED** or **UNCERTAIN**.
8. The conveyor and **servo gate** route the item.
9. Raw values, corrected values, timing and the decision are logged.

Correction rule used for each channel:

```
corrected = MAX(0, measurement_with_illumination - measurement_without_illumination)
```

### Why two channels?

Published USDA work on fecal contamination of apples found that reflectance imaging alone was inadequate for thin smears, while UV fluorescence detected them, and that two-band ratios improved sensitivity. Combining reflectance and fluorescence gives more features than a single UV threshold. It still does not prove the presence or absence of a specific contaminant. See [References](#references).

---

## Hardware

| Function | Component |
|---|---|
| Controller | Teensy 4.1 (600 MHz Cortex-M7, 3.3 V logic) |
| Visible reflectance | TCS34725 RGB + clear sensor with white LED |
| UV excitation | UV LED ring, about 390-395 nm, switched by relay |
| Fluorescence detection | BPW34 photodiode with optical filter and analog front-end |
| Item detection | IR sensor |
| Conveyor drive | TB6612FNG dual motor driver + DC motor |
| Sorting gate | SG90 servo |
| Display (optional) | I2C OLED |
| User input | Manual button, status LED |

---

## Pin map

Treat this table as the firmware interface definition. Keep all pin numbers in a **single source-of-truth config section** in the firmware, and update it with any wiring change.

| Function | Teensy pin | Interface | Notes |
|---|---|---|---|
| I2C SDA | 18 | I2C | TCS34725 + OLED bus |
| I2C SCL | 19 | I2C | TCS34725 + OLED bus |
| TCS34725 LED enable | 2 | Digital out | Visible illumination control |
| UV relay / UV ring | 3 | Digital out | Relay observed as **active-low** in prototype |
| IR trigger | 4 | Digital in | Firmware assumes active-HIGH; **verify sensor polarity** |
| SG90 servo signal | 5 | PWM | Sorting gate |
| TB6612 AIN1 | 6 | Digital out | Motor direction |
| TB6612 AIN2 | 7 | Digital out | Motor direction |
| TB6612 PWMA | 8 | PWM | Motor speed |
| TB6612 STBY | 9 | Digital out | Held HIGH during normal operation |
| Manual button | 10 | Digital in | INPUT_PULLUP |
| Status LED | 13 | Digital out | Built-in Teensy LED |
| BPW34 analog | 14 (A0) | ADC | UV fluorescence channel |

### Electrical notes

- All Teensy GPIO must stay within **3.3 V** logic levels.
- Use a separate motor rail for the TB6612 and keep a shared, intentional ground reference.
- Route motor and relay wiring away from the photodiode front-end. Decouple supplies to limit switching noise on the analog channel.
- Keep the BPW34 amplifier output within the Teensy ADC input range.
- Wire the UV ring through the relay **COM and NO** contacts so UV is OFF when the controller is unpowered. Confirm behavior on the physical relay module.

---

## Firmware state machine

A deterministic state machine prevents motor, UV, servo and sensor activity from interfering with each other.

| State | Purpose | Key actions |
|---|---|---|
| `BOOT` | Initialize hardware | Safe GPIO states, I2C, TCS34725, OLED, servo, motor driver |
| `CALIBRATE` | Establish baseline | Dark readings, sensor sanity checks, optional startup calibration |
| `IDLE` | Wait for item | Conveyor ready, illumination off, monitor IR |
| `SCANNING` | Acquire data | Visible scan, UV scan, timing and averaging |
| `CLASSIFY` | Compute result | Background correction, feature extraction, decision |
| `ACT` | Physical response | Conveyor motion and/or servo gate |
| `LOG` | Record result | Serial, OLED, CSV |
| `FAULT` | Safe recovery | Disable optics/actuation as appropriate, request operator attention |

Normal cycle:

```
IDLE → IR TRIGGER → SCANNING → CLASSIFY → ACT → LOG → IDLE
```

---

## Signal processing and decision logic

### Acquisition

- Multiple samples per measurement window.
- Mean for symmetric noise, median or trimmed mean when spikes or bus disturbances occur.
- Standard deviation is recorded so a stable high response can be distinguished from a noisy one.

### Validity checks

- Detect saturation or near-saturation.
- Reject impossible values and invalid I2C transactions.
- Mark excessive noise or unstable readings as **UNCERTAIN** instead of forcing PASS or FLAGGED.
- Always keep raw measurements for diagnostics.

### Features

| Type | Features |
|---|---|
| Raw | R, G, B, C, UV (corrected), noise, timing |
| Derived | R/G, G/B, R/B, UV/C, UV relative to local background |

Ratios can reduce sensitivity to absolute intensity but amplify noise when the denominator is small, so each feature should be evaluated statistically on real calibration data.

### Decision model evolution

| Level | Model | Use |
|---|---|---|
| Prototype 1 | Simple threshold | Hardware bring-up and channel verification |
| Prototype 2 | Multi-feature thresholds | Combine UV and reflectance |
| Prototype 3 | Statistical classifier | Distributions from known-good and reference samples |
| Prototype 4 | ML classifier | Labeled datasets, after measurement quality is proven |

Decision boundaries should come from measured distributions and the relative cost of false positives and false negatives. An **UNCERTAIN** band is recommended for borderline samples.

---

## Calibration and riboflavin reference

Calibrate with the same optics, sample-to-sensor distance, conveyor speed, exposure timing and illumination used in routine operation.

**Clean baseline**
- Collect many samples judged clean (using an external reference method where feasible).
- Repeat across separate runs and days.
- Record mean, standard deviation, range and any sub-populations.
- Repeat at different item positions within the allowed mechanical tolerance.

**Reference-positive data**
- Should represent the specific target the system is meant to screen for.

**Riboflavin (vitamin B2)** is used as an **optical reference standard** only.

| Question | Riboflavin can show | Riboflavin cannot show |
|---|---|---|
| Does the UV source excite a fluorescent material? | Yes | Whether contamination is present |
| Does the filter/photodiode path respond? | Yes | Whether a given contaminant has the same spectrum |
| Does UV-ON minus UV-OFF give a measurable delta? | Yes | A validated food-safety threshold |
| Can it check repeatability? | Yes | Target-specific validation |

A strong riboflavin response means the fluorescence measurement chain works. It is not evidence of contamination.

---

## Data logging

Serial/CSV record:

```
timestamp, item_id, r_light, g_light, b_light, c_light,
r_dark, g_dark, b_dark, c_dark,
r_corr, g_corr, b_corr, c_corr,
uv_light, uv_dark, uv_corr, noise, result, cycle_ms
```

Keep measurement data separate from the decision label so the raw dataset is not contaminated by a premature classification.

| Field group | Example fields |
|---|---|
| Identity | timestamp, sample_id, run_id |
| Environment | ambient condition notes, setup identifier |
| Visible raw / corrected | r_light ... c_dark, r_corr ... c_corr |
| UV raw / corrected | uv_light, uv_dark, uv_corr |
| Quality | noise, saturation_flag, sensor_valid |
| Decision | result, confidence/score if available |
| Actuation | gate_angle, conveyor_time, action_status |

Optional OLED screens: READY, SCANNING, RESULT, FAULT, CALIBRATION. The system must keep working over Serial if the display is absent.

---

## Fault handling

| Fault | Detection | Safe response |
|---|---|---|
| TCS34725 missing | I2C init/read fails | Disable measurement; FAULT or Serial-only diagnostic mode |
| OLED missing | Display not found | Continue without OLED |
| IR stuck active | Trigger stays active beyond expected window | Stop accepting new items; report fault |
| Relay/UV fault | No expected switching or unexpected active state | Block classification; flag maintenance |
| BPW34 saturation/noise | ADC near rail or variance too high | Mark measurement invalid / UNCERTAIN |
| Servo fault | Position/timeout issue if feedback exists | Stop conveyor or use maintenance-safe state |
| Motor driver fault | No expected motion / external monitoring | Stop system; report operator fault |

---

## UV safety

390-395 nm UV can be hazardous to eyes and skin.

- Enclose the optical station and minimize direct exposure.
- Use engineering controls and operating procedures suited to the actual LED power and enclosure design.
- Keep UV OFF as the safe default state (relay wired COM/NO).
- Never look directly at the UV LEDs; wear UV-blocking eyewear when working on an open station.

---

## Testing and validation plan

| Phase | Test | Example pass criteria |
|---|---|---|
| A | Power and GPIO sanity | Outputs reach commanded states; no unsafe startup actuation |
| B | I2C sensor test | TCS34725 responds consistently at expected address |
| C | UV channel test | UV ON/OFF gives a repeatable delta on a fluorescent reference |
| D | Dark stability | Background stays within an established noise envelope |
| E | Repeatability | Same reference measured repeatedly with bounded variation |
| F | Geometry test | Small position changes do not destroy class separation |
| G | Timing test | Measurement + actuation fits the conveyor timing budget |
| H | System test | PASS / FLAGGED / UNCERTAIN trigger correct physical actions |
| I | Target-specific validation | Performance evaluated against appropriate reference/lab results |

Metrics to capture: mean and standard deviation per feature, false-positive and false-negative counts, repeatability across runs and days, cycle time and sorting latency, percentage of UNCERTAIN results, and sensor dropout and fault rates.

---

## Roadmap

| Stage | Goal | Output |
|---|---|---|
| 1 | Hardware bring-up | Stable TCS34725, BPW34, UV relay/ring, IR, servo, conveyor |
| 2 | Optical repeatability | Stable geometry, dark subtraction, noise characterization |
| 3 | Reference testing | Clean baseline + fluorescent reference checks |
| 4 | Target-specific dataset | Known/reference samples with external ground truth |
| 5 | Decision model | Thresholds or statistical classifier validated on held-out data |
| 6 | Inline automation | Reliable conveyor timing and gate operation |
| 7 | Enclosure and safety | Light control, UV containment, wiring, maintenance design |
| 8 | Production-oriented validation | Long-run testing, drift monitoring, service procedures |

---

## Known issues and things to verify

- [ ] **IR sensor polarity:** firmware assumes active-HIGH. Confirm on the real sensor.
- [ ] **Relay polarity:** prototype relay behaves active-low (LOW = ON). Confirm on the physical module and check 3.3 V input compatibility.
- [ ] **Filter vs. reference:** riboflavin emits green light (peak near 530 nm). If the BPW34 uses a red filter, check its transmission curve before using riboflavin to validate the optical path.
- [ ] **UV threshold** is a placeholder and needs calibration data.
- [ ] **TCS34725 availability:** sources disagree on the part's lifecycle status. Verify before ordering more.

---

## Repository structure

```
food-contamination-screening/
├── README.md
├── docs/          Architecture PDF, references
├── firmware/      Teensy 4.1 code
├── hardware/      Wiring diagrams, pin map, BOM
└── data/          Calibration and test CSV logs
```

---

## Getting started

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and the **Teensyduino** add-on.
2. Clone the repo:
   ```
   git clone https://github.com/YOUR-USERNAME/food-contamination-screening.git
   ```
3. Open the sketch in `firmware/`, select **Teensy 4.1** as the board, and edit the pin configuration section if your wiring differs.
4. Wire the hardware following the [pin map](#pin-map). Bring up subsystems one at a time (Stage 1).
5. Open the Serial Monitor to view CSV output and run the calibration phases in the [testing plan](#testing-and-validation-plan).

---

## References

**Fluorescence-based food inspection**
- Kim et al. (2002). *Multispectral detection of fecal contamination on apples, Part II: Hyperspectral fluorescence imaging.* Trans. ASAE. [PDF](https://ars.usda.gov/ARSUserFiles/953/2002Kim45-6TransASAE2039-2047.pdf)
- Kim et al. (2005). *Automated detection of fecal contamination of apples based on multispectral fluorescence image fusion.* [AGRIS record](https://agris.fao.org/search/en/records/65df2e604c5aef494fe0842f)
- Lefcourt, Kim & Chen (2003). *Automated detection of fecal contamination of apples by multispectral laser-induced fluorescence imaging.* Applied Optics. [PDF](https://www.ars.usda.gov/ARSUserFiles/3003/2003Lefcourt42-19ApplOpt3935-3943.pdf)
- US Patent 7,787,111. *Simultaneous acquisition of fluorescence and reflectance imaging with a single imaging device.* [PDF](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7787111)
- Huang, Liu & Ngadi (2014). *Recent developments in hyperspectral imaging for assessment of food quality and safety.* Sensors. [Article](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4029639/)

**Components**
- [ams-OSRAM TCS34725](https://ams-osram.com/de/products/sensor-solutions/ambient-light-color-spectral-proximity-sensors/ams-tcs34725-color-sensor)
- [Vishay BPW34](https://www.vishay.com/en/product/81521/)
- [PJRC Teensy 4.1](https://www.pjrc.com/store/teensy41.html)
- [Pololu TB6612FNG](https://www.pololu.com/product/713)

**Reference material and safety**
- [USP Riboflavin monograph](https://www.drugfuture.com/Pharmacopoeia/USP35/data/v35300/usp35nf30s0_m73500.html)
- [OMLC PhotochemCAD, riboflavin spectra](https://omlc.org/spectra/PhotochemCAD/html/004.html)
- [SPIE: Ocular UV protection, revisiting safe limits](https://proceedings.spiedigitallibrary.org/conference-proceedings-of-spie/8567/85671K/Ocular-UV-protection--revisiting-safe-limits-for-sunglasses-standards/10.1117/12.2000355.full)





## License

Add your chosen license here (for example, MIT) and include a `LICENSE` file in the repo root.
