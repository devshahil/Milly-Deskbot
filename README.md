# 🤖 DeskBot v3.1 — Expressive Desktop Companion Robot

[![ESP32-C3](https://img.shields.io/badge/Platform-ESP32--C3%20Super%20Mini-blue.svg?logo=espressif&style=for-the-badge)](https://www.espressif.com/)
[![Arduino IDE](https://img.shields.io/badge/Framework-Arduino-00979D.svg?logo=arduino&style=for-the-badge)](https://www.arduino.cc/)
[![FreeRTOS](https://img.shields.io/badge/OS-FreeRTOS-orange.svg?style=for-the-badge)](https://www.freertos.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)
[![Status: Active](https://img.shields.io/badge/Version-v3.1%20Companion-success.svg?style=for-the-badge)](#)

> **DeskBot v3.1** is an autonomous, expressive, and interactive AI-like desktop companion robot powered by the ultra-compact **ESP32-C3 Super Mini**. Equipped with an animated TFT eye engine, an ultrasonic radar head that sweeps and locks onto nearby humans, capacitive touch petting, 6-axis IMU physical gesture sensing, an integrated dark-mode web dashboard, NTP clock, Pomodoro productivity workstation assistant, live weather mood synchronization, and intelligent battery telemetry.

---

## 📑 Table of Contents

- [Overview & Architecture](#-overview--architecture)
- [✨ Key Features](#-key-features)
- [🎭 Emotion & Personality System](#-emotion--personality-system)
- [📦 Bill of Materials (Parts List)](#-bill-of-materials-parts-list)
- [📐 Wiring & Pinout Guide](#-wiring--pinout-guide)
  - [Pin Assignment Table](#pin-assignment-table)
  - [Power & Battery Management Circuit](#power--battery-management-circuit)
- [🚀 Step-by-Step Build Guide](#-step-by-step-build-guide)
  - [Step 1: Arduino IDE & Toolchain Configuration](#step-1-arduino-ide--toolchain-configuration)
  - [Step 2: Installing Required Libraries](#step-2-installing-required-libraries)
  - [Step 3: Bench Wiring & Hardware Verification](#step-3-bench-wiring--hardware-verification)
  - [Step 4: Compiling & Flashing Firmware](#step-4-compiling--flashing-firmware)
  - [Step 5: WiFi Provisioning via Captive Portal](#step-5-wifi-provisioning-via-captive-portal)
  - [Step 6: Mechanical Assembly & 3D Enclosure](#step-6-mechanical-assembly--3d-enclosure)
  - [Step 7: Room Calibration & First Run](#step-7-room-calibration--first-run)
- [🎮 User Controls & Interactions](#-user-controls--interactions)
- [🌐 Web Dashboard & REST API](#-web-dashboard--rest-api)
- [🔧 Customization & Configuration Options](#-customization--configuration-options)
- [📋 Troubleshooting & FAQ](#-troubleshooting--faq)
- [📄 License](#-license)

---

## 🧠 Overview & Architecture

DeskBot is designed to sit right under your computer monitor or on your workspace, bringing your desk to life while actively aiding your productivity. It does not rely on cloud audio processing or expensive GPUs; all procedural rendering, kinematics, sensor fusion, radar mapping, and physics simulations run locally in real-time on the single-core 32-bit RISC-V ESP32-C3 microcontroller.

```
                           +--------------------------+
                           |  1S Li-ion / USB Type-C  |
                           +------------+-------------+
                                        |
                 +----------------------+----------------------+
                 |                                             |
         +-------v--------+                             +------v-------+
         | TP4056 Charger |                             | 5V DC Boost  |
         +-------+--------+                             +------+-------+
                 |                                             |
+----------------v---------------------------------------------v---------------------+
|                               ESP32-C3 Super Mini                                  |
|                                                                                    |
|  [Hardware Peripherals]                                                            |
|    - SPI Master  ------> ST7735 / ILI9341 16-bit TFT (Double-Buffered Canvas)      |
|    - I2C Master  ------> MPU-6050 6-Axis IMU (Tilt, Shake, Flip, Dizziness)        |
|    - GPIO / ADC  ------> Battery Voltage Divider (100k/100k + 100nF Filter)        |
|    - Digital I/O ------> TTP223 Touch Pad (Petting, Tickle, Comforting)           |
|    - Precision PWM ----> SG90 Micro Servo (Head Sweep & Motion Smoothing)         |
|    - Pulse Timing -----> HC-SR04 / HC-SR04P Ultrasonic Sensor (Radar Guard)       |
|    - Tactile Btn ------> Multi-function Mode & Reset Switch                        |
|                                                                                    |
|  [Software Subsystems]                                                             |
|    * Dual-layer Procedural Eye Renderer (Saccades, Blinking, Tear Simulation)      |
|    * FreeRTOS Task: Background HTTPS Open-Meteo Weather Polling                   |
|    * 9-Sector Room Calibration & Active Target Tracker Engine                     |
|    * WebServer + REST Endpoints + mDNS ("deskbot.local")                           |
|    * NTP Timekeeping + Pomodoro State Machine + Alarm Monitor                      |
+------------------------------------------------------------------------------------+
```

---

## ✨ Key Features

### 👁️ Procedural Expressive Eye Engine
- **Vector-like Canvas Rendering**: Renders smooth rounded eyes into an internal 16-bit double buffer (`GFXcanvas16`) pushed synchronously via SPI to eliminate screen tearing and flickering.
- **Natural Micro-Motions**: Autonomous eye saccades, randomized gaze wandering, intelligent blinking, and double-blinks.
- **Parametric Eyelids & Cheeks**: Dynamically blends upper lid droop, angled eyebrow slants (anger/sadness), and cheek squashes (happiness) using real-time interpolation math.

### 📡 Radar Room Patrol & Target Tracking (Guard Mode)
- **Automatic Room Baseline Calibration**: On startup, sweeps from 30° to 150° in 9 distinct sectors, taking median samples to map the static obstacles and geometry of your desk.
- **Autonomous Intruder / Person Tracking**: When an object moves closer than `DETECT_MARGIN` (20 cm closer than baseline), DeskBot locks its eyes, activates cyber HUD brackets, turns its servo head directly toward the subject, and tracks them horizontally.
- **Interactive "Boop"**: Move within 10 cm of its nose and DeskBot gets delightfully startled and happy.
- **Friend Recognition**: If someone remains close for over 5 seconds, it greets them warmly. If they leave, it displays a sad expression before resuming patrol.

### 🖐️ Physical Touch & 6-Axis IMU Sensor Fusion
- **Petting**: Stroke the top-mounted TTP223 capacitive pad to trigger rising pixel-art heart particles.
- **Tickling & Annoyance**: Double-tap quickly to tickle; tap 4 times in rapid succession and DeskBot gives you an irritated angry glare.
- **Long Cuddles**: Hold the touch pad for >6 seconds to make DeskBot cosy and sleepy.
- **Gravity-Responsive Eyes**: Tilt the robot left, right, up, or down—its pupils naturally slide with the gravitational vector thanks to the MPU-6050 accelerometer.
- **Progressive Shake Distress**:
  1. *Gentle shake* &rarr; **Scared** (wide pupils, jittery wobble, cold sweat drops).
  2. *Continuous shake* &rarr; **Sad** (drooping eyelids, rolling tears).
  3. *Violent shake* &rarr; **Angry** (pulsing Japanese anime-style anger vein).
  4. *Overwhelmed shake* &rarr; **Crying** (inconsolable sobbing and double streams of tears).
  - *The Only Cure*: When crying, shaking ceases to work; DeskBot will only calm down once you gently touch and comfort its head!
- **Upside-Down Flip**: Turn DeskBot upside down to put it to sleep with floating "z" animations. When flipped back upright, its eyes spin in a circle from dizziness.

### ⏱️ Desktop Productivity Workstation
- **NTP Digital Clock**: Ultra-accurate time synchronization over WiFi with 12h/24h formats, seconds readout, day/date, battery indicator, and IP address overlay.
- **Smart Night Mode**: Automatically dims into a peaceful sleeping face between 22:00 and 06:00 when idle.
- **Pomodoro Timer**: Complete 25-minute focus intervals and 5-minute break routines (durations customizable via web dashboard) with graphical progress bars, cycle counters, and a determined `FOCUS` face during work blocks.
- **Stopwatch**: Centisecond-resolution digital stopwatch with instant start/pause/reset.
- **Daily Alarm**: Built-in daily alarm with a flashing alert screen.

### 🌦️ Real-Time Weather Moods
- Spawns a background **FreeRTOS task** that fetches local meteorological conditions via the free HTTPS [Open-Meteo API](https://open-meteo.com/) (zero API keys needed).
- Displays current temperature (°C) and condition text ("Clear", "Rain", "Snow", "Fog", "Thunderstorm") on the clock.
- Ambient weather periodically influences DeskBot's mood (e.g., cheerful on sunny days, sleepy in thick fog, grumpy during thunderstorms, or melancholic in the rain).

### 🌐 Embedded Web Dashboard & REST API
- Zero mobile apps required. Just visit `http://deskbot.local` or its local IP address on any desktop browser, iPhone, or Android device.
- Dark-mode responsive dashboard offering real-time status telemetry, mode switching, manual mood overrides, Pomodoro controls, alarm scheduling, geolocation setup, and radar recalibration.

### 🔋 Intelligent Battery Telemetry & Power Saving
- Measures 1S Li-ion / LiPo cells (3.0 V – 4.2 V) via a hardware voltage divider and ADC smoothing filter.
- Calculates remaining capacity using an authentic multi-point non-linear discharge curve.
- Alerts user with a flashing on-screen battery icon when charge falls below 15%; enters deep sleepy power conservation mode at $\le 5\%$.
- Auto-detaches the servo PWM signal when stationary to eliminate motor buzzing, prevent jitter, and save power.

---

## 🎭 Emotion & Personality System

| Mood | Screen Color | Expression & Visual Details | Trigger Condition |
| :--- | :---: | :--- | :--- |
| **NORMAL** | Cyan (`#00FFFF`) | Calm, natural gaze wandering, random blinking | Default resting state |
| **HAPPY** | Green (`#00FF00`) | Raised curved cheeks, heart particles floating up | Gentle pet, proximity boop, friendly greeting |
| **SLEEPY** | Pale Blue (`#867F`) | Half-closed lids, floating `"z"` letters | Night hours (22:00-06:00), flipped upside down, $\le 5\%$ battery, 6s cuddle |
| **ANGRY** | Bright Red (`#FF0000`) | Steep down-angled eyebrows, trembling eyes, pulsing anger vein | 4 rapid poke taps, sustained rough shaking, severe thunderstorms |
| **SAD** | Royal Blue (`#041F`) | Downcast pupils, downturned brow, rolling tear drops | Rainy weather, tracked friend walks away |
| **FOCUS** | Amber Orange (`#FD20`) | Intense narrowed stare, animated HUD corner targeting brackets | Active Pomodoro work session, radar target lock-on |
| **DEPRESSED**| Slate Blue (`#4AAD`) | Drooping weary eyes with an animated raincloud raining overhead | Extended isolation, dreary weather |
| **SCARED** | Stark White (`#F7BE`) | Shivering dilated pupils, cold sweat droplets falling | Shaken briefly |
| **CRYING** | Squeezed Blue (`#041F`) | Rapid vertical sobbing jitter, double streaming tear tracks | Sustained violent shaking (*requires a comforting head pat to calm*) |

---

## 📦 Bill of Materials (Parts List)

### 1. Primary Electronics

| Component | Qty | Specification / Recommended Model | Purpose |
| :--- | :---: | :--- | :--- |
| **Microcontroller** | 1 | **ESP32-C3 Super Mini** (RISC-V 160MHz, built-in WiFi/BLE) | Main processing unit |
| **TFT Display** | 1 | **1.8" SPI TFT LCD (ST7735, 160x128)** *(or ILI9341 240x320)* | Displaying animated eyes, clock, and menus |
| **Servo Motor** | 1 | **SG90 9g Micro Servo** (or metal-gear MG90S) | Horizontal radar panning and head movement |
| **Distance Sensor** | 1 | **HC-SR04** (5V) or **HC-SR04P** (3.3V compatible) | Ultrasonic proximity detection and room mapping |
| **IMU Sensor** | 1 | **MPU-6050 (GY-521)** 6-DOF Gyro & Accelerometer | Gesture, tilt, flip, and shake detection |
| **Touch Sensor** | 1 | **TTP223** Capacitive Touch Sensor Module | Petting and physical touch interaction |
| **Tactile Button** | 1 | 6x6mm Momentary Pushbutton | Mode switching, timer control, WiFi reset |

### 2. Power & Passive Components

| Component | Qty | Value / Details | Purpose |
| :--- | :---: | :--- | :--- |
| **Li-ion Battery** | 1 | 1S 3.7V Li-ion / LiPo (18650 cell or 500–1200mAh pouch) | Untethered portable power |
| **Charging Board** | 1 | **TP4056 USB-C** with DW01A battery protection | Safe battery charging over USB-C |
| **Step-Up Boost** | 1 | **5V DC-DC Boost Module** (MT3608 or micro 5V 1A boost) | Providing steady 5V to servo and ultrasonic sensor |
| **Power Switch** | 1 | SPDT Miniature Slide Switch | Hardware power On/Off |
| **Schottky Diode** | 1 | **1N5819** (or equivalent 1A low-drop Schottky) | Prevents back-feeding boost converter when USB is plugged in |
| **Decoupling Cap** | 1 | **470µF – 1000µF 10V/16V Electrolytic Capacitor** | Absorbs servo current spikes; prevents ESP32 brownouts |
| **Filter Cap** | 1 | **100nF (0.1µF) Ceramic Capacitor** | Noise filtering on the battery ADC sensing pin |
| **Resistors** | 2 | **100 kΩ (1/4W, 1% tolerance)** | Voltage divider for 1S battery monitoring |
| **Resistors (Level)**| 1 ea| **1 kΩ & 2 kΩ** *(Only needed for 5V HC-SR04)* | 5V to 3.3V level divider for the ultrasonic ECHO pin |

### 3. Hardware & Mechanicals
- 3D printed robot chassis (head, base housing, and bracket for HC-SR04).
- 28–30 AWG flexible silicone hookup wire.
- M2 and M3 screws for mounting servo and boards.
- Double-sided foam tape or hot glue for securing sensors.

---

## 📐 Wiring & Pinout Guide

### Pin Assignment Table

> [!IMPORTANT]
> The GPIO numbers listed in the firmware correspond directly to the silk-screened pin numbers printed on the **ESP32-C3 Super Mini** board.

| Peripheral | Peripheral Pin | ESP32-C3 Super Mini GPIO | Notes / Requirements |
| :--- | :--- | :---: | :--- |
| **ST7735 Display** | SCK / CLK | **GPIO 4** | Hardware SPI Clock |
| | SDA / MOSI | **GPIO 6** | Hardware SPI Data |
| | A0 / DC | **GPIO 5** | Data / Command select |
| | RESET / RST | **GPIO 3** | Hardware reset |
| | CS | **GPIO 7** | Chip Select |
| | VCC / LED | **3.3V** | Connect backlight (LED) & VCC to 3.3V |
| | GND | **GND** | Common ground |
| **MPU-6050** | VCC | **3.3V** | Operating voltage 3.3V |
| | GND | **GND** | Common ground |
| | SDA | **GPIO 8** | Hardware I2C Data |
| | SCL | **GPIO 9** | Hardware I2C Clock |
| | AD0 | **GND** | Pull to GND for I2C address `0x68` |
| **TTP223 Touch** | VCC | **3.3V** | Connect to 3.3V |
| | GND | **GND** | Common ground |
| | OUT | **GPIO 20** | Active HIGH digital output |
| **Pushbutton** | Pin 1 | **GPIO 10** | Uses internal `INPUT_PULLUP` |
| | Pin 2 | **GND** | Short to GND on press |
| **SG90 Servo** | Orange / Signal | **GPIO 21** | 50Hz PWM output |
| | Red / Power | **5V Rail** | **Do NOT power from ESP32 3.3V!** Connect to 5V rail. |
| | Brown / Ground| **GND** | Connect a 470µF capacitor across Red and Brown |
| **HC-SR04 Sensor** | VCC | **5V Rail** | Connect to 5V rail (or 3.3V if using HC-SR04P) |
| | GND | **GND** | Common ground |
| | TRIG | **GPIO 2** | Digital trigger output |
| | ECHO | **GPIO 1** | **Voltage Divider required** (see diagram below) |
| **Battery Sense** | Divider Tap | **GPIO 0** | Reads smoothed voltage through 100k/100k divider |

---

### Power & Battery Management Circuit

```
  [1S 3.7V Li-ion / LiPo]
          |
    +-----+-----+
    |  B+   B-  |
    |  TP4056   |  (USB-C 5V Charging Input)
    | OUT+ OUT- |
    +-----+-----+
          |
      [SPDT Switch]
          |
          +-----------------------+---> [100k Resistor] ---> GPIO 0 ---> [100k Resistor] ---> GND
          |                       |                            |
          |                  (Battery+)                 [100nF Cap to GND]
          |
   +------v------+
   |  5V Boost   |  (Step-up converter)
   | OUT+   OUT- |
   +------+------+
          |
     [1N5819 Diode]
          |
          +======================= 5V Rail =======================+
          |                               |                      |
   +------v------+                 +------v------+        +------v------+
   | ESP32-C3 5V |                 |  SG90 Servo |        | HC-SR04 VCC |
   +-------------+                 +-------------+        +-------------+
                                          |
                                   [470µF Cap across
                                     5V and GND]
```

#### Ultrasonic Sensor 5V to 3.3V Voltage Divider
Because standard HC-SR04 modules output a 5V logic pulse on their `ECHO` pin, you **must** drop this voltage to protect the ESP32-C3's 3.3V input:
```
HC-SR04 ECHO Pin ----[ 1 kΩ Resistor ]----+---- GPIO 1 (ESP32-C3)
                                          |
                                   [ 2 kΩ Resistor ]
                                          |
                                         GND
```
*(If using the 3.3V-capable **HC-SR04P**, connect VCC to 3.3V and ECHO directly to GPIO 1 without resistors).*

---

## 🚀 Step-by-Step Build Guide

### Step 1: Arduino IDE & Toolchain Configuration

1. Download and install the latest **[Arduino IDE 2.x](https://www.arduino.cc/en/software)**.
2. Open **File &rarr; Preferences**.
3. Under **Additional Boards Manager URLs**, append the official Espressif ESP32 board URL:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Navigate to **Tools &rarr; Board &rarr; Boards Manager**, search for `esp32`, and install version **2.0.14** (or 3.x).
5. In the **Tools** menu, select:
   - **Board**: `"ESP32C3 Dev Module"`
   - **USB CDC On Boot**: `"Enabled"` *(Crucial for Serial console output on Super Mini)*
   - **Flash Size**: `"4MB (32Mb)"`
   - **Partition Scheme**: `"Huge APP (3MB No OTA/1MB SPIFFS)"` *(Required: WiFiManager + SSL takes ~1.8MB flash)*
   - **Upload Speed**: `921600` (or `460800` if flashing fails)

---

### Step 2: Installing Required Libraries

Open **Tools &rarr; Manage Libraries...** (`Ctrl+Shift+I`) and install:

| Library Name | Author | Version / Notes |
| :--- | :--- | :--- |
| **Adafruit GFX Library** | Adafruit | Core 2D graphics primitive engine |
| **Adafruit ST7735 and ST7789 Library** | Adafruit | High-speed driver for 1.8" TFT |
| **WiFiManager** | tzapu | Captive portal WiFi configuration |
| **ESP32Servo** | Kevin Harrington | High-resolution hardware PWM servo library for ESP32 |

*(Optional: If you are using a 2.4" or 2.8" ILI9341 display instead of the ST7735, install **Adafruit ILI9341** and set `#define USE_ST7735 0` in line 58 of `DeskBot_v3.1.ino`).*

---

### Step 3: Bench Wiring & Hardware Verification

1. Place the **ESP32-C3 Super Mini** onto a solderless breadboard.
2. Wire the SPI display pins to **GPIO 3, 4, 5, 6, 7** according to the pinout table.
3. Wire the **MPU-6050** to I2C lines (**GPIO 8** = SDA, **GPIO 9** = SCL) and pull `AD0` to `GND`.
4. Connect the **TTP223** touch sensor `OUT` to **GPIO 20**.
5. Connect the tactile switch between **GPIO 10** and `GND`.
6. Assemble the 100k/100k voltage divider on **GPIO 0** for battery monitoring.
7. Connect the **SG90 Servo** signal wire to **GPIO 21** and ensure the 470µF capacitor is installed across the 5V and GND rails.
8. Wire the **HC-SR04** trigger (**GPIO 2**) and echo voltage divider (**GPIO 1**).

---

### Step 4: Compiling & Flashing Firmware

1. Open `DeskBot_v3.1.ino` in your Arduino IDE.
2. Connect your ESP32-C3 Super Mini to your computer via a high-quality USB-C data cable.
3. Select your ESP32's COM port under **Tools &rarr; Port**.
4. Click **Upload** (`Ctrl+U`).
5. Once compiled and uploaded, open the **Serial Monitor** at `115200 baud` to view the startup sequence.

> [!TIP]
> If the ESP32-C3 fails to enter upload mode automatically, press and hold the **BOOT** button on the Super Mini, tap the **RST** button once, and release **BOOT** right as the IDE prints `"Connecting..."`.

---

### Step 5: WiFi Provisioning via Captive Portal

1. On the very first boot, the TFT screen will display:
   ```text
   Join WiFi:
   DeskBot-Setup
   then open
   192.168.4.1
   ```
2. Using your smartphone or laptop, open your WiFi settings and connect to the access point named **`DeskBot-Setup`**.
3. A captive portal page will automatically pop up (or navigate manually to `http://192.168.4.1` in your browser).
4. Tap **Configure WiFi**, select your local 2.4 GHz home/office WiFi network, enter your password, and tap **Save**.
5. DeskBot will connect to your network, synchronize its clock via NTP, display its assigned IP address and `deskbot.local` URL on the screen, and transition into autonomous mode!

> [!NOTE]
> Need to change WiFi networks later? Simply **hold down the physical button on GPIO 10 while powering up DeskBot**. It will immediately forget the old credentials and open the `DeskBot-Setup` portal again.

---

### Step 6: Mechanical Assembly & 3D Enclosure

1. **Mounting the Servo**: Secure the SG90 servo body firmly into the base chassis pointing upwards.
2. **Mounting the HC-SR04 "Eyes"**: Fasten the HC-SR04 ultrasonic sensor directly onto the servo horn or a custom head bracket so the sensor rotates left and right as the servo turns.
3. **Display Placement**: Mount the TFT screen centered in the robot's face frame.
4. **Touch Plate**: Affix the TTP223 touch pad directly underneath the top shell of the head or robot "hat" so the user can pet DeskBot by touching the outer plastic.
5. **IMU Orientation**: Fix the MPU-6050 flat inside the main chassis so that standard horizontal desk orientation is registered as neutral rest gravity.

---

### Step 7: Room Calibration & First Run

1. Place DeskBot on your desk where it will live, facing forward toward the room.
2. Ensure there are no temporary items (hands, mugs, boxes) within 1.5 meters directly in front of the bot.
3. Power on DeskBot. After boot, it will print:
   ```text
   Learning
   the room...
   stay clear!
   ```
4. The head will systematically sweep from 30° to 150°, taking median distance measurements at 9 angles to memorize your room layout.
5. Once finished, DeskBot returns to center (90°), looks around, and is ready to keep you company!

---

## 🎮 User Controls & Interactions

### Physical Button Operations (GPIO 10)

| Action | Duration | Result |
| :--- | :---: | :--- |
| **Short Press** | $< 600\text{ ms}$ | Cycles to next mode: `Eyes` &rarr; `Clock` &rarr; `Pomodoro` &rarr; `Stopwatch` |
| **Medium Press** | $600\text{ ms} - 2000\text{ ms}$ | **Context Action**:<br>&bull; *In Eyes mode*: Cycles through 9 moods manually<br>&bull; *In Clock mode*: Toggles daily alarm ON/OFF<br>&bull; *In Pomodoro mode*: Starts / Pauses the timer<br>&bull; *In Stopwatch mode*: Starts / Stops the timer |
| **Long Hold** | $> 2000\text{ ms}$ | Resets the active Pomodoro cycle or Stopwatch to zero |
| **Hold at Boot** | During Power-On | Clears saved WiFi network credentials and launches setup portal |

---

### Touch & Motion Gestures

```
     [GENTLE STROKE]                  [RAPID TAPS]                   [LONG CUDDLE]
     Touch TTP223 Pad               Tap 4 times fast              Hold touch > 6s
            │                              │                             │
            ▼                              ▼                             ▼
       Rising Hearts                 Angry Reaction                 Sleepy Face
      (Happy & Loving)               (Pulsing Vein)                 (Cosy & Calm)
```

- **Petting**: Gently tap or stroke the head &rarr; Rising pink pixel hearts and cheerful green eyes.
- **Tickle**: Double-tap within 2 seconds &rarr; DeskBot gives a surprised happy giggle.
- **Annoy**: Tap 4 times consecutively &rarr; DeskBot gets frustrated, narrowing its eyes into a red angry glare.
- **Physical Tilts**: Tilt the robot left/right/forward &rarr; The pupils roll realistically toward the tilt direction.
- **Inverted Flip**: Turn upside down &rarr; Eyes close into sleep mode with floating `"z"`s. Return right side up &rarr; DeskBot spins its pupils in dizziness for 2 seconds.
- **Comforting a Crying Bot**: If shaken excessively, DeskBot breaks into uncontrollable sobbing and tears. The only way to stop the crying is to tenderly place your hand on its touch sensor!

---

## 🌐 Web Dashboard & REST API

DeskBot hosts a lightweight, mobile-responsive dark-mode web application on port 80. Access it at:
- **Local mDNS**: `http://deskbot.local`
- **Direct IP**: `http://<YOUR_DESKBOT_IP>` *(Shown on the clock screen)*

```
+------------------------------------------+
|                 DeskBot                  |
|                 14:32:08                 |
|  Mode: Eyes | Mood: normal | Batt: 88%   |
+------------------------------------------+
| Mode:                                    |
| [ Eyes ] [ Clock ] [ Pomodoro ] [ Stopw ]|
+------------------------------------------+
| Controls:                                |
| [ Pomodoro Start/Pause ]   [ Reset ]     |
| [ Stopwatch Start/Stop ]   [ Reset ]     |
| [ Stop Alarm ]      [ Recalibrate Room ] |
+------------------------------------------+
| Mood Override:                           |
| [ Auto ▾ ]                               |
+------------------------------------------+
| Settings:                                |
| Work (min):       [ 25 ]                 |
| Break (min):      [ 5  ]                 |
| Alarm Time:       [ 07:00 ]  [x] Alarm On|
| UTC Offset (hrs): [ 5.5 ]    [x] 24h Clk |
| Latitude:         [ 28.613 ]             |
| Longitude:        [ 77.209 ] [x] Weather |
| Idle to Clock (s):[ 300 ]    [x] Guard   |
| [ Save Settings ]                        |
+------------------------------------------+
```

### REST API Documentation

DeskBot can easily be integrated into Home Assistant, Node-RED, or external automation scripts via simple HTTP GET requests:

#### 1. Fetch System Telemetry
```http
GET /api/status
```
**JSON Response**:
```json
{
  "time": "14:32:08",
  "modeName": "Eyes",
  "mood": "happy",
  "pomo": "work 24:12",
  "pRun": 1,
  "done": 3,
  "work": 25,
  "brk": 5,
  "alarm": "07:00",
  "aon": 1,
  "tz": 5.5,
  "h24": 1,
  "snd": 1,
  "lat": 28.613,
  "lon": 77.209,
  "idle": 300,
  "guard": 1,
  "wxon": 1,
  "wx": "Clear 24C",
  "track": 1,
  "bat": 88,
  "mv": 4015
}
```

#### 2. Execute Actions
```http
GET /api/do?a=<action>&v=<value>
```
| Action (`a`) | Parameter (`v`) | Description |
| :--- | :--- | :--- |
| `mode` | `0` (Eyes), `1` (Clock), `2` (Pomo), `3` (Stopwatch) | Changes the current display mode |
| `mood` | `-1` (Auto), `0` to `8` (Mood enum ID) | Overrides or restores the emotional state |
| `pomo` | *none* | Starts or pauses the Pomodoro timer |
| `pomoreset` | *none* | Resets Pomodoro timer back to configured work duration |
| `sw` | *none* | Starts or stops the digital stopwatch |
| `swreset` | *none* | Resets stopwatch to `00:00.00` |
| `stopalarm` | *none* | Silences active alarm |
| `calib` | *none* | Commands the robot to immediately re-scan and calibrate the room |

#### 3. Save Configuration
```http
GET /api/save?work=25&brk=5&alarm=07:30&aon=1&tz=5.5&h24=1&snd=1&lat=28.613&lon=77.209&idle=300&guard=1&wx=1
```
All parameters are committed to persistent non-volatile ESP32 flash (`Preferences.h`) and persist across power cycles.

---

## 🔧 Customization & Configuration Options

All primary hardware definitions and operational thresholds can be modified near the top of [DeskBot_v3.1.ino](file:///c:/Users/LOQ/Downloads/bot/DeskBot_v3.1.ino#L58-L82):

```cpp
// 1 = 1.8" ST7735 (160x128), 0 = ILI9341 (240x320)
#define USE_ST7735 1

// Pin definitions
#define TFT_SCK       4
#define TFT_MOSI      6
#define TFT_CS        7
#define TFT_DC        5
#define TFT_RST       3
#define BTN_PIN      10
#define SDA_PIN       8
#define SCL_PIN       9
#define TOUCH_PIN    20
#define SERVO_PIN    21
#define TRIG_PIN      2
#define ECHO_PIN      1
#define BATT_PIN      0

// Behavioral thresholds
#define SHAKE_JERK    0.45f   // Sensitivity of MPU-6050 shake detection (lower = more sensitive)
#define NIGHT_SLEEPS  1       // 1 = Sleep face between 22:00-06:00 instead of digital clock
#define DETECT_MARGIN 20.0f   // Distance in cm closer than baseline needed to trigger target lock
```

---

## 📋 Troubleshooting & FAQ

### Q1: The display shows inverted colors or strange offset borders.
In `setup()`, find the initialization line:
```cpp
tft.initR(INITR_BLACKTAB);
```
Depending on your Chinese clone ST7735 display manufacturer, swap `INITR_BLACKTAB` to `INITR_GREENTAB`, `INITR_REDTAB`, or `INITR_18BLACKTAB`.

### Q2: The ESP32 reboots randomly whenever the servo moves.
This is a classic **brownout reset**. Servos draw sudden peak current spikes of up to 500mA–1A when starting up.
- Never power the servo from the ESP32's onboard 3.3V pin.
- Place a **470µF or 1000µF electrolytic capacitor** directly across the 5V and GND terminals powering the SG90 servo.

### Q3: Ultrasonic sensor always reads 999 cm or 0 cm.
- Ensure you built the 1kΩ / 2kΩ voltage divider on the `ECHO` pin.
- If using an older HC-SR04, it requires a full 5V supply to operate; powering it from 3.3V will cause timeouts. If you wish to power it from 3.3V directly, verify that your sensor is an **HC-SR04P** or **RCWL-9610**.

### Q4: The battery percentage shows `-1`.
When connected strictly via the USB-C port without a connected battery, the voltage on GPIO 0 drops below 2.5V, and the code automatically detects you are running on direct USB power, hiding the battery meter.

---

## 📄 License

This project is licensed under the **MIT License** — you are free to modify, build, distribute, and integrate this project into your own companion robots and commercial creations.

---

<p align="center">
  <b>Crafted with ❤️ for makers, robotics enthusiasts, and desk dwellers.</b><br>
  <i>Give DeskBot a ⭐ on GitHub if it brightened up your workspace!</i>
</p>

