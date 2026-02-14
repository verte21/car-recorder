# CarRecorder

A smart, battery-powered voice-activated audio recorder built with the **Seeed Studio XIAO ESP32S3 Sense**. Designed for in-vehicle use, it detects conversation-level audio and records it to an SD card as standard WAV files — while ignoring continuous road and engine noise.

Perfect for hands-free voice logging, dashcam audio companions, or personal audio journaling during commutes.

## Features

- **Voice-activated recording** — triggers on sounds above the adaptive noise floor, ignores ambient noise
- **Adaptive noise floor** — continuously calibrates to the environment (quiet room, idling car, highway)
- **2-minute recording chunks** — stops after 2 minutes; immediately re-triggers if sound is still present
- **Automatic file rotation** — splits recordings at 10 minutes for data safety
- **SD card recovery** — detects write failures and automatically reinitializes the SD bus
- **Low-power operation** — runs at 80MHz to maximize battery life on an 18650 cell
- **Standard WAV output** — 16kHz, 16-bit mono files playable on any device

## Components

| Component            | Description                                                   |
| -------------------- | ------------------------------------------------------------- |
| XIAO ESP32S3 Sense   | Microcontroller with built-in PDM microphone and microSD slot |
| 18650 Li-Ion battery | 3.7V rechargeable cell for portable power                     |
| 18650 battery holder | With leads or integrated charging (TP4056 module)             |
| microSD card         | FAT32 formatted, Class 10 or better recommended               |

> [!NOTE]
> The XIAO ESP32S3 Sense includes the microphone and SD card slot on its expansion board — no extra breakout boards needed.

## Wiring

No external wiring required for the microphone or SD card — both are integrated on the Sense expansion board.

For battery power, connect the 18650 cell to the XIAO's battery pads:

- **BAT+** → 18650 positive
- **BAT-** → 18650 negative

The XIAO has a built-in charge controller, so plugging in USB will charge the battery automatically.

## Installation

### 1. Arduino IDE Setup

1. Open **Arduino IDE** (2.x recommended)
2. Go to **File → Preferences** and add this board manager URL:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Board Manager**, search for **esp32** and install the **esp32 by Espressif Systems** package

### 2. Board Selection

1. Go to **Tools → Board** and select **XIAO_ESP32S3**
2. Select the correct **Port** (your USB serial port)

### 3. Upload

1. Open `recorder/recorder.ino` in Arduino IDE
2. Click **Upload** (→ button)
3. Wait for the upload to complete

### 4. Prepare the SD Card

1. Format a microSD card as **FAT32** (32KB clusters recommended for performance)
2. Insert it into the XIAO Sense expansion board's microSD slot
3. Power the device — recording starts automatically when sound is detected

## Configuration

All tunable parameters are at the top of `recorder.ino`:

```cpp
// TIMING
const int RECORDING_TIME_MIN = 2;    // Each recording chunk length (minutes)
const int MAX_RECORDING_MIN = 10;    // Max file length before rotation (minutes)

// AUDIO
const float GAIN = 45.0;             // Microphone gain (picks up whispers)

// TRIGGER
const int SPIKE_SENSITIVITY = 1500;  // Start trigger (above noise floor)

// NOISE FLOOR
const float NOISE_FLOOR_MAX = 15000; // Cap (high enough for 45x gain environments)
const float NOISE_FLOOR_INIT = 100;  // Initial noise floor estimate
```

### Tuning Tips

- **Too many false triggers?** → Increase `SPIKE_SENSITIVITY`
- **Missing quiet speech?** → Increase `GAIN` or decrease `SPIKE_SENSITIVITY`
- **Want longer recording chunks?** → Increase `RECORDING_TIME_MIN`
- **Want longer uninterrupted files?** → Increase `MAX_RECORDING_MIN`

## How It Works

```
┌─────────────┐    sound detected    ┌─────────────────┐
│   STANDBY   │ ──────────────────→  │   RECORDING     │
│             │                      │   (2 min timer) │
│ noise floor │    timer expired     │                 │
│ adapting    │ ←──────────────────  │ all audio saved │
└─────────────┘                      │                 │
       ↑                             │  10 min limit → │
       │          noise continues    │  rotate file    │
       └──────── immediately ←───── └─────────────────┘
                re-triggers
```

1. **Standby** — device listens and adapts to ambient noise level (fast: 0.98/0.02 filter)
2. **Trigger** — sound above `noiseFloor + 1500` starts a 2-minute recording
3. **Recording** — all audio is captured (voice, silence, everything). Noise floor continues adapting slowly (0.998/0.002)
4. **Stop** — after 2 minutes, the file is finalized and the device returns to standby
5. **Re-trigger** — if sound is still present, a new recording starts immediately (consecutive files)
6. **Rotation** — if a recording exceeds 10 minutes, the file is seamlessly split
7. **Recovery** — if SD writes fail 3 times in a row, the SD bus is reinitialized automatically

## Output Files

Files are saved to the SD card root with zero-padded numbering:

```
/car_0001.wav
/car_0002.wav
/car_0003.wav
...
```

Each file is a standard 16kHz, 16-bit mono WAV file (~3.8MB per 2-minute chunk). File numbering continues from where it left off across reboots.

## Playback

The recommended tool for listening to recordings is **[Audacity](https://www.audacityteam.org/download/)** (free, open-source, available on Windows/macOS/Linux).

1. Remove the microSD card and plug it into your computer
2. Open Audacity and drag in any `car_*.wav` file
3. The files are 16kHz mono — Audacity will handle them automatically

### Useful Audacity Features

- **Noise Reduction** (`Effect → Noise Reduction`) — select a quiet section as the noise profile, then apply to the whole track to clean up road noise
- **Amplify** (`Effect → Amplify`) — boost quiet passages
- **Spectrogram view** (`Track menu → Spectrogram`) — visually identify speech segments in long recordings

> [!TIP]
> If you have multiple consecutive files from one conversation, import them all into Audacity. Use `Tracks → Align Tracks → Align End to End` to stitch them together seamlessly.

## LED Indicator

The onboard orange LED (GPIO 21) shares a pin with the SD chip select:

| LED State      | Meaning                                  |
| -------------- | ---------------------------------------- |
| Off            | Standby — listening for sound            |
| Rapid blinking | Recording — writing audio to SD          |
| Slow 1s blinks | SD card full — replace or clear the card |

## SD Card Tips

- Use a **Class 10 / U1 or better** card (A2 rating ideal for small writes)
- Format as **FAT32 with 32KB clusters** for best performance
- Periodically move files off the card to avoid directory bloat
- A 32GB card holds ~140 hours of recordings (~5,600 files)

## License

MIT
