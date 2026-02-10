#include "ESP_I2S.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp32-hal-cpu.h"

// --- HIGHWAY FILTER CONFIGURATION ---

// 1. TIMING
const int INITIAL_TIME_MIN = 2;     // Initial recording window after trigger
const int EXTENSION_TIME_MIN = 2;   // Extension added per confirmed speech spike
const int SPIKES_TO_EXTEND = 2;     // How many loud spikes confirm a conversation?
const int MAX_RECORDING_MIN = 10;   // Max single WAV file length (safety limit)

// 2. AUDIO
const float GAIN = 45.0;            // Very high gain (picks up whispers)

// 3. THRESHOLDS
const int SPIKE_SENSITIVITY = 800;       // Easy start (wakes up the device)
const int EXTENSION_SENSITIVITY = 2000;  // Hard extension (ignores tire noise, catches voice)

// 4. NOISE FLOOR
const float NOISE_FLOOR_MAX = 500;       // Noise floor cap (prevents trigger lockout)
const float NOISE_FLOOR_INIT = 100;      // Initial value

// Pins (Xiao ESP32S3 Sense)
const int8_t I2S_CLK = 42;
const int8_t I2S_DIN = 41;
const int    SD_CS   = 21;
// NOTE: LED shares pin with SD_CS (21) — blinks naturally during SD writes

const uint32_t SAMPLERATE = 16000;
I2SClass I2S;
File audioFile;

// System variables
float filteredZero = 0;
float noiseFloor = NOISE_FLOOR_INIT;
bool isRecording = false;
unsigned long stopTime = 0;
int spikeCounter = 0;
unsigned long lastSpikeMs = 0;
int fileIndex = 0;
int saveCounter = 0;
unsigned long fileStartTime = 0;   // When the current file started

// Buffer
int16_t buffer[512];
int bufIdx = 0;

// Write empty header (reserve space)
void writeDummyHeader(File &file) {
  uint8_t header[44];
  memset(header, 0, 44);
  file.write(header, 44);
}

// Finalize WAV header (writes correct sizes and closes file)
void finalizeHeader(File &file) {
  unsigned long dataSize = file.size() - 44;
  unsigned long overallSize = dataSize + 36;
  unsigned long byteRate = SAMPLERATE * 2;

  uint8_t header[44];
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  memcpy(&header[4], &overallSize, 4);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  uint32_t fmtSize = 16; memcpy(&header[16], &fmtSize, 4);
  uint16_t fmtType = 1; memcpy(&header[20], &fmtType, 2);
  uint16_t channels = 1; memcpy(&header[22], &channels, 2);
  memcpy(&header[24], &SAMPLERATE, 4);
  memcpy(&header[28], &byteRate, 4);
  uint16_t align = 2; memcpy(&header[32], &align, 2);
  uint16_t bits = 16; memcpy(&header[34], &bits, 2);
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  memcpy(&header[40], &dataSize, 4);

  file.seek(0);
  file.write(header, 44);
  file.close();
}

// Flush remaining buffer before closing file
void flushBuffer() {
  if (bufIdx > 0 && audioFile) {
    // Pad remaining buffer with silence
    while (bufIdx < 512) {
      buffer[bufIdx++] = 0;
    }
    audioFile.write((uint8_t*)buffer, 1024);
    bufIdx = 0;
  }
}

void setup() {
  // Eco mode (80MHz) — save battery
  setCpuFrequencyMhz(80);

  if (!SD.begin(SD_CS)) {
    while(1) delay(100);
  }

  while (SD.exists("/car_" + String(fileIndex) + ".wav")) {
    fileIndex++;
  }

  I2S.setPinsPdmRx(I2S_CLK, I2S_DIN);
  if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLERATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    while(1) delay(100);
  }

  // Quick calibration
  long sum = 0;
  int validSamples = 0;
  for(int i=0; i<500; i++) {
    int r = I2S.read();
    if(r!=0) { sum += r; validSamples++; }
  }
  if (validSamples > 0) {
    filteredZero = sum / (float)validSamples;
  }
}

void startRecording() {
  String fileName = "/car_" + String(fileIndex) + ".wav";
  audioFile = SD.open(fileName, FILE_WRITE);

  if(audioFile) {
    writeDummyHeader(audioFile);
    bufIdx = 0;
    saveCounter = 0;
    isRecording = true;
    fileStartTime = millis();

    // Set 2-minute timer (only for new sessions, not file rotations)
    if (spikeCounter == 0) {
      stopTime = millis() + (INITIAL_TIME_MIN * 60000UL);
      spikeCounter = 1;
      lastSpikeMs = millis();
    }

  }
}

// File rotation (seamless — session continues in a new file)
void rotateFile() {
  flushBuffer();
  finalizeHeader(audioFile);
  fileIndex++;
  saveCounter = 0;

  // Open new file — session continues
  String fileName = "/car_" + String(fileIndex) + ".wav";
  audioFile = SD.open(fileName, FILE_WRITE);
  if (audioFile) {
    writeDummyHeader(audioFile);
    bufIdx = 0;
    fileStartTime = millis();
  } else {
    // Failed to open — end session
    isRecording = false;
    spikeCounter = 0;
    noiseFloor = NOISE_FLOOR_INIT;
  }
}

// End recording session
void stopRecording() {
  flushBuffer();
  finalizeHeader(audioFile);
  isRecording = false;
  fileIndex++;
  saveCounter = 0;
  spikeCounter = 0;

  // Reset noise floor to baseline (so next trigger works easily)
  noiseFloor = NOISE_FLOOR_INIT;
}

void loop() {
  long signalSum = 0;
  int sampleCount = 0;
  unsigned long chunkStart = millis();

  // Collect samples (20ms window)
  while (millis() - chunkStart < 20) {
    int raw = I2S.read();

    // --- AUDIO PROCESSING ---
    filteredZero = (filteredZero * 0.99) + (raw * 0.01);
    float pureWave = raw - filteredZero;
    float amplified = pureWave * GAIN;

    // Limiter
    if (amplified > 32767) amplified = 32767;
    if (amplified < -32768) amplified = -32768;

    int16_t finalSample = (int16_t)amplified;

    // For volume analysis, ignore zero reads (keeps average reliable)
    if (raw != 0) {
       signalSum += abs(finalSample);
       sampleCount++;
    }

    // --- WRITE TO SD ---
    if (isRecording) {
      buffer[bufIdx++] = finalSample;

      if (bufIdx >= 512) {
        audioFile.write((uint8_t*)buffer, 1024);
        bufIdx = 0;

        // Safety flush every ~12 seconds
        saveCounter++;
        if (saveCounter > 400) {
          audioFile.flush();
          saveCounter = 0;
        }
      }
    }
  }

  // Average volume of this chunk (20ms)
  float currentVolume = 0;
  if (sampleCount > 0) currentVolume = signalSum / sampleCount;

  // --- HIGHWAY FILTER LOGIC ---

  // Two thresholds:
  // 1. Easy (800) — triggers recording start
  float startTrigger = noiseFloor + SPIKE_SENSITIVITY;

  // 2. Hard (2000) — extends recording (tire noise won't pass, voice will)
  float extensionTrigger = noiseFloor + EXTENSION_SENSITIVITY;

  bool startSignal = (currentVolume > startTrigger);
  bool extensionSignal = (currentVolume > extensionTrigger);

  // Debounce (1 second gap between spikes)
  bool debounceOk = (millis() - lastSpikeMs > 1000);

  if (!isRecording) {
    // STANDBY MODE
    // Update noise floor (with cap!)
    noiseFloor = (noiseFloor * 0.98) + (currentVolume * 0.02);
    if (noiseFloor > NOISE_FLOOR_MAX) noiseFloor = NOISE_FLOOR_MAX;

    // Wake up on any loud sound
    if (startSignal && debounceOk) {
      startRecording();
    }

  } else {
    // RECORDING MODE

    // Extend ONLY if sound is very loud/distinct (speech)
    // Steady tire noise won't exceed the extension threshold
    if (extensionSignal && debounceOk) {
      spikeCounter++;
      lastSpikeMs = millis();

      // Confirmed conversation (enough spikes)
      if (spikeCounter >= SPIKES_TO_EXTEND) {
        // Extend by 2 minutes from now
        unsigned long newStopTime = millis() + (EXTENSION_TIME_MIN * 60000UL);
        if (newStopTime > stopTime) {
          stopTime = newStopTime;
        }
      }
    }

    // Safety: max file length (rotate file, session continues)
    if (millis() - fileStartTime > (MAX_RECORDING_MIN * 60000UL)) {
      rotateFile();
    }

    // Check if session timer expired
    if (millis() > stopTime) {
      // No recent spikes = conversation over, back to standby
      stopRecording();
    }
  }
}