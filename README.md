# CarRecorder

A smart, battery-powered voice-activated audio recorder built with the **Seeed Studio XIAO ESP32S3 Sense**. Designed for in-vehicle use, it intelligently detects conversation-level audio and records it to an SD card as standard WAV files — while filtering out continuous road and engine noise.

Perfect for hands-free voice logging, dashcam audio companions, or personal audio journaling during commutes.

## Features

- **Adaptive noise floor** — automatically calibrates to the ambient sound level, so it works whether the car is idling or on the highway
- **Two-threshold detection** — easy trigger to start recording, hard threshold to confirm ongoing speech and extend the session
- **Smart session management** — records for 2 minutes by default; extends automatically when voice activity is confirmed
- **Automatic file rotation** — splits long recordings into 10-minute WAV files for data safety (if power is lost, only the last chunk is affected)
- **Low-power operation** — runs at 80MHz to maximize battery life on an 18650 cell
- **Standard WAV output** — 16kHz, 16-bit mono files playable on any device

## Components

| Component            | Description                                                   |
| -------------------- | ------------------------------------------------------------- |
| XIAO ESP32S3 Sense   | Microcontroller with built-in PDM microphone and microSD slot |
| 18650 Li-Ion battery | 3.7V rechargeable cell for portable power                     |
| 18650 battery holder | With leads or integrated charging (TP4056 module)             |
| microSD card         | FAT32 formatted, Class 10 recommended                         |

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

1. Open `recorder.ino` in Arduino IDE
2. Click **Upload** (→ button)
3. Wait for the upload to complete

### 4. Prepare the SD Card

1. Format a microSD card as **FAT32**
2. Insert it into the XIAO Sense expansion board's microSD slot
3. Power the device — recording starts automatically when sound is detected

## Configuration

All tunable parameters are at the top of `recorder.ino`:

```cpp
// TIMING
const int INITIAL_TIME_MIN = 2;     // Initial recording window (minutes)
const int EXTENSION_TIME_MIN = 2;   // Extension per confirmed voice spike (minutes)
const int SPIKES_TO_EXTEND = 2;     // Spikes needed to confirm conversation
const int MAX_RECORDING_MIN = 10;   // Max file length before rotation (minutes)

// AUDIO
const float GAIN = 45.0;            // Microphone gain

// THRESHOLDS
const int SPIKE_SENSITIVITY = 800;       // Start trigger (above noise floor)
const int EXTENSION_SENSITIVITY = 2000;  // Extension trigger (voice only)

// NOISE FLOOR
const float NOISE_FLOOR_MAX = 500;       // Max noise floor (prevents lockout)
const float NOISE_FLOOR_INIT = 100;      // Initial noise floor estimate
```

### Tuning Tips

- **Too many false triggers?** → Increase `SPIKE_SENSITIVITY`
- **Missing quiet speech?** → Increase `GAIN` or lower `EXTENSION_SENSITIVITY`
- **Recordings too short?** → Decrease `SPIKES_TO_EXTEND` to 1
- **Want longer uninterrupted files?** → Increase `MAX_RECORDING_MIN`

## How It Works

```
┌─────────────┐    sound detected    ┌─────────────────┐
│   STANDBY   │ ──────────────────→  │   RECORDING     │
│             │                      │   (2 min timer) │
│ noise floor │    timer expired     │                 │
│ adapting    │ ←──────────────────  │  voice spikes   │
└─────────────┘   no speech found    │  extend timer   │
                                     │                 │
                                     │  10 min limit → │
                                     │  rotate file    │
                                     └─────────────────┘
```

1. **Standby** — device listens and adapts to ambient noise level
2. **Trigger** — any sound above `noiseFloor + 800` starts a 2-minute recording
3. **Extension** — loud/distinct sounds (above `noiseFloor + 2000`) confirm speech; after 2 confirmed spikes, the timer extends by 2 more minutes
4. **Stop** — when the timer expires without recent spikes, the file is finalized and the device returns to standby
5. **Rotation** — if a recording exceeds 10 minutes, the file is seamlessly split

## Output Files

Files are saved to the SD card root as:

```
/car_0.wav
/car_1.wav
/car_2.wav
...
```

Each file is a standard 16kHz, 16-bit mono WAV file. File numbering continues from where it left off (even across reboots).

## Playback

The recommended tool for listening to recordings is **[Audacity](https://www.audacityteam.org/download/)** (free, open-source, available on Windows/macOS/Linux).

1. Remove the microSD card and plug it into your computer
2. Open Audacity and drag in any `car_*.wav` file
3. The files are 16kHz mono — Audacity will handle them automatically

### Useful Audacity Features

- **Noise Reduction** (`Effect → Noise Reduction`) — select a quiet section as the noise profile, then apply to the whole track to clean up road noise
- **Amplify** (`Effect → Amplify`) — boost quiet passages
- **Spectrogram view** (`Track menu → Spectrogram`) — visually identify speech segments in long recordings
- **Label Tracks** — mark interesting moments for quick navigation

> [!TIP]
> If you have multiple files from one session (due to file rotation), you can import them all into Audacity and they'll appear as separate tracks. Use `Tracks → Align Tracks → Align End to End` to stitch them together.

## LED Indicator

The onboard LED shares a pin with the SD chip select — it blinks naturally during SD card writes. When recording is active, you'll see steady rapid blinking. In standby, the LED is off.

## License

MIT
