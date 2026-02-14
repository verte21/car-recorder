#include "ESP_I2S.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "esp32-hal-cpu.h"


// 1. TIMING
const int RECORDING_TIME_MIN = 2;    // Each recording chunk length (minutes)
const int MAX_RECORDING_MIN = 10;    // Max single WAV file length (safety limit)

// 2. AUDIO
const float GAIN = 45.0;             // Very high gain (picks up whispers)

// 3. TRIGGER
const int SPIKE_SENSITIVITY = 1500;  // Start trigger — additive above noise floor

// 4. NOISE FLOOR
const float NOISE_FLOOR_MAX = 15000; // Cap (high enough for 45x gain environments)
const float NOISE_FLOOR_INIT = 100;  // Initial value

// Pins (Xiao ESP32S3 Sense)
const int8_t I2S_CLK = 42;
const int8_t I2S_DIN = 41;
const int    SD_CS   = 21;
// LED shares pin with SD_CS (21) — blinks naturally during SD writes
// When SD is full, we deinit SD and use pin 21 purely as LED alert

const uint32_t SAMPLERATE = 16000;
I2SClass I2S;
File audioFile;

// System variables
float filteredZero = 0;
float noiseFloor = NOISE_FLOOR_INIT;
bool isRecording = false;
unsigned long stopTime = 0;
int fileIndex = 0;
int saveCounter = 0;
unsigned long fileStartTime = 0;
int writeFailCount = 0;             // Consecutive SD write failures

// Buffer (2KB = fewer SD writes, less wear)
int16_t buffer[1024];
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
    while (bufIdx < 1024) {
      buffer[bufIdx++] = 0;
    }
    audioFile.write((uint8_t*)buffer, 2048);
    bufIdx = 0;
  }
}

void setup() {
  // Eco mode (80MHz) — save battery
  setCpuFrequencyMhz(80);

  if (!SD.begin(SD_CS)) {
    while(1) delay(100);
  }

  char checkName[20];
  sprintf(checkName, "/car_%04d.wav", fileIndex);
  while (SD.exists(checkName)) {
    fileIndex++;
    sprintf(checkName, "/car_%04d.wav", fileIndex);
  }

  I2S.setPinsPdmRx(I2S_CLK, I2S_DIN);
  if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLERATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    while(1) delay(100);
  }

  // Warm-up — PDM mic outputs garbage right after init
  for(int i=0; i<1000; i++) {
    I2S.read();
  }

  // Quick DC offset estimate (loop self-corrects)
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

// SD card full — deinit SD, blink LED with long pulses
void sdFullHalt() {
  SD.end();
  pinMode(SD_CS, OUTPUT);
  while(1) {
    digitalWrite(SD_CS, HIGH);
    delay(1000);
    digitalWrite(SD_CS, LOW);
    delay(1000);
  }
}

// Check SD free space (returns true if OK or unsupported)
bool sdHasFreeSpace(unsigned long minMB) {
  unsigned long total = SD.totalBytes();
  unsigned long used = SD.usedBytes();
  if (total == 0 || used > total) return true;
  unsigned long freeMB = (total - used) / (1024 * 1024);
  return freeMB >= minMB;
}

void startRecording() {
  if (!sdHasFreeSpace(5)) {
    sdFullHalt();
  }

  char fileName[20];
  sprintf(fileName, "/car_%04d.wav", fileIndex);
  audioFile = SD.open(fileName, FILE_WRITE);

  // If open fails, reinit SD bus and retry once
  if (!audioFile) {
    SD.end();
    delay(100);
    if (SD.begin(SD_CS)) {
      audioFile = SD.open(fileName, FILE_WRITE);
    }
  }

  if(audioFile) {
    writeDummyHeader(audioFile);
    bufIdx = 0;
    saveCounter = 0;
    isRecording = true;
    fileStartTime = millis();
    stopTime = millis() + (RECORDING_TIME_MIN * 60000UL);
  }
}

// File rotation (seamless — recording continues in a new file)
void rotateFile() {
  flushBuffer();
  finalizeHeader(audioFile);
  fileIndex++;
  saveCounter = 0;

  char fileName[20];
  sprintf(fileName, "/car_%04d.wav", fileIndex);
  audioFile = SD.open(fileName, FILE_WRITE);
  if (audioFile) {
    writeDummyHeader(audioFile);
    bufIdx = 0;
    fileStartTime = millis();
  } else {
    isRecording = false;
  }
}

// End recording
void stopRecording() {
  flushBuffer();
  finalizeHeader(audioFile);
  isRecording = false;
  fileIndex++;
  saveCounter = 0;
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

    // Volume analysis (ignore zero reads)
    if (raw != 0) {
       signalSum += abs(finalSample);
       sampleCount++;
    }

    // --- WRITE TO SD ---
    if (isRecording) {
      buffer[bufIdx++] = finalSample;

      if (bufIdx >= 1024) {
        size_t written = audioFile.write((uint8_t*)buffer, 2048);
        bufIdx = 0;

        if (written != 2048) {
          // Write failed
          writeFailCount++;
          if (writeFailCount >= 3) {
            // SD is dying — stop, reinit, recover
            audioFile.close();
            isRecording = false;
            SD.end();
            delay(200);
            SD.begin(SD_CS);
            fileIndex++;
            writeFailCount = 0;
            break;  // Exit sample loop, return to standby
          }
        } else {
          writeFailCount = 0;  // Reset on successful write
        }

        // Safety flush every ~48 seconds
        saveCounter++;
        if (saveCounter > 400) {
          audioFile.flush();
          saveCounter = 0;

          // Check if SD is almost full
          if (!sdHasFreeSpace(1)) {
            stopRecording();
            sdFullHalt();
          }
        }
      }
    }
  }

  // Average volume of this chunk (20ms)
  float currentVolume = 0;
  if (sampleCount > 0) currentVolume = signalSum / sampleCount;

  // Trigger threshold
  float startTrigger = noiseFloor + SPIKE_SENSITIVITY;

  if (!isRecording) {
    // STANDBY — adapt noise floor, wait for trigger
    noiseFloor = (noiseFloor * 0.98) + (currentVolume * 0.02);
    if (noiseFloor > NOISE_FLOOR_MAX) noiseFloor = NOISE_FLOOR_MAX;

    if (currentVolume > startTrigger) {
      startRecording();
    }

  } else {
    // RECORDING — adapt noise floor slowly, check timers
    noiseFloor = (noiseFloor * 0.998) + (currentVolume * 0.002);
    if (noiseFloor > NOISE_FLOOR_MAX) noiseFloor = NOISE_FLOOR_MAX;

    // Safety: max file length (rotate, recording continues)
    if (millis() - fileStartTime > (MAX_RECORDING_MIN * 60000UL)) {
      rotateFile();
    }

    // Timer expired — stop recording, back to standby
    if (millis() > stopTime) {
      stopRecording();
    }
  }
}