#include <Arduino.h>
#include "driver/i2s.h"
#include "SD.h"
#include "FS.h"
#include "SPI.h"

// I2S Microphone Pins (CORRECT for CrowPanel 2.8")
#define I2S_SD          10    // Data pin
#define I2S_WS          3     // Word Select (LRCLK)
#define I2S_SCK         9     // Bit Clock (BCLK)
#define MIC_ENABLE      45    // IMPORTANT: Must be HIGH to enable mic!
#define I2S_PORT        I2S_NUM_0

// SD Card Pins
#define SD_CS           7
#define SD_MOSI         6
#define SD_MISO         4
#define SD_SCK          5

// Recording Settings
#define SAMPLE_RATE     16000
#define BUFFER_SIZE     512
#define GAIN_FACTOR     4     // Adjust if needed (1-8)

// WAV Header Structure
typedef struct {
  char riff[4];
  uint32_t flength;
  char wave[4];
  char fmt[4];
  uint32_t chunk_size;
  uint16_t format_tag;
  uint16_t num_chans;
  uint32_t srate;
  uint32_t bytes_per_sec;
  uint16_t bytes_per_samp;
  uint16_t bits_per_samp;
  char data[4];
  uint32_t dlength;
} WAV_HEADER;

File audioFile;
bool recording = false;
uint32_t fileCounter = 0;
SPIClass sdSPI(HSPI);
int currentGain = GAIN_FACTOR;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n========================================");
  Serial.println("CrowPanel 2.8\" Audio Recorder");
  Serial.println("========================================\n");
  
  // CRITICAL: Enable microphone power!
  pinMode(MIC_ENABLE, OUTPUT);
  digitalWrite(MIC_ENABLE, HIGH);
  Serial.println("✓ Microphone power enabled (GPIO45)");
  delay(100);
  
  // Initialize I2S
  if (!initI2S()) {
    Serial.println("❌ FAILED to initialize I2S!");
    while(1) delay(1000);
  }
  
  // Initialize SD Card
  if (!initSDCard()) {
    Serial.println("❌ FAILED to initialize SD Card!");
    while(1) delay(1000);
  }
  
  Serial.println("\n========================================");
  Serial.println("✓✓✓ System Ready! ✓✓✓");
  Serial.println("========================================");
  Serial.println("\nCommands:");
  Serial.println("  'r' - Start recording");
  Serial.println("  's' - Stop recording");
  Serial.println("  'l' - List files on SD card");
  Serial.println("  't' - Test microphone (10 sec)");
  Serial.println("  'd' - Delete all audio files");
  Serial.println("  '+' - Increase gain");
  Serial.println("  '-' - Decrease gain");
  Serial.printf("\nCurrent gain: %dx\n", currentGain);
  Serial.println("========================================\n");
}

void loop() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    switch(cmd) {
      case 'r':
      case 'R':
        if (!recording) startRecording();
        else Serial.println("⚠ Already recording!");
        break;
        
      case 's':
      case 'S':
        if (recording) stopRecording();
        else Serial.println("⚠ Not recording!");
        break;
        
      case 'l':
      case 'L':
        listFiles();
        break;
        
      case 't':
      case 'T':
        if (!recording) testMicrophone();
        else Serial.println("⚠ Stop recording first!");
        break;
        
      case 'd':
      case 'D':
        if (!recording) deleteAllAudioFiles();
        else Serial.println("⚠ Stop recording first!");
        break;
        
      case '+':
        currentGain = min(currentGain + 1, 16);
        Serial.printf("✓ Gain: %dx\n", currentGain);
        break;
        
      case '-':
        currentGain = max(currentGain - 1, 1);
        Serial.printf("✓ Gain: %dx\n", currentGain);
        break;
    }
  }
  
  if (recording) {
    recordAudio();
  }
  
  delay(10);
}

bool initI2S() {
  Serial.println("Initializing I2S Microphone...");
  Serial.printf("  Pins: SD=%d, WS=%d, SCK=%d\n", I2S_SD, I2S_WS, I2S_SCK);
  
  // CORRECT configuration for CrowPanel microphone
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,  // 16-bit, not 32!
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,   // Standard I2S format
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };
  
  // Uninstall if exists
  i2s_driver_uninstall(I2S_PORT);
  
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("  ✗ I2S install failed: %d\n", err);
    return false;
  }
  
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("  ✗ I2S pin config failed: %d\n", err);
    return false;
  }
  
  Serial.println("  ✓ I2S Microphone Ready");
  Serial.printf("  Sample Rate: %d Hz, 16-bit Mono\n", SAMPLE_RATE);
  
  return true;
}

bool initSDCard() {
  Serial.println("Initializing SD Card...");
  
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS, sdSPI, 80000000)) {
    if (!SD.begin(SD_CS, sdSPI, 40000000)) {
      Serial.println("  ✗ SD Card mount failed!");
      return false;
    }
  }
  
  if (SD.cardType() == CARD_NONE) {
    Serial.println("  ✗ No SD card!");
    return false;
  }
  
  Serial.println("  ✓ SD Card Ready");
  Serial.printf("  Size: %llu MB\n", SD.cardSize() / (1024 * 1024));
  
  return true;
}

void startRecording() {
  char filename[32];
  sprintf(filename, "/audio_%04d.wav", fileCounter++);
  
  Serial.printf("\n🔴 Recording...\n");
  Serial.printf("   File: %s\n", filename);
  Serial.printf("   Gain: %dx\n", currentGain);
  
  audioFile = SD.open(filename, FILE_WRITE);
  if (!audioFile) {
    Serial.println("❌ Cannot create file!");
    return;
  }
  
  // Write WAV header
  WAV_HEADER header;
  memcpy(header.riff, "RIFF", 4);
  header.flength = 0;
  memcpy(header.wave, "WAVE", 4);
  memcpy(header.fmt, "fmt ", 4);
  header.chunk_size = 16;
  header.format_tag = 1;
  header.num_chans = 1;
  header.srate = SAMPLE_RATE;
  header.bytes_per_sec = SAMPLE_RATE * 2;
  header.bytes_per_samp = 2;
  header.bits_per_samp = 16;
  memcpy(header.data, "data", 4);
  header.dlength = 0;
  
  audioFile.write((uint8_t*)&header, sizeof(WAV_HEADER));
  
  // Clear I2S buffer
  i2s_zero_dma_buffer(I2S_PORT);
  
  recording = true;
  Serial.println("   Press 's' to stop\n");
}

void recordAudio() {
  static uint32_t totalBytes = 0;
  static uint32_t lastPrint = 0;
  static int16_t peakLevel = 0;
  
  int16_t buffer[BUFFER_SIZE];
  size_t bytes_read = 0;
  
  esp_err_t result = i2s_read(I2S_PORT, buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);
  
  if (result == ESP_OK && bytes_read > 0) {
    int samples = bytes_read / sizeof(int16_t);
    
    // Apply gain and track levels
    for (int i = 0; i < samples; i++) {
      int32_t sample = buffer[i] * currentGain;
      
      // Clamp to prevent clipping
      if (sample > 32767) sample = 32767;
      if (sample < -32768) sample = -32768;
      
      buffer[i] = (int16_t)sample;
      
      // Track peak
      int16_t absLevel = abs(buffer[i]);
      if (absLevel > peakLevel) peakLevel = absLevel;
    }
    
    // Write to SD card
    size_t written = audioFile.write((uint8_t*)buffer, bytes_read);
    totalBytes += written;
    
    // Show progress every second
    if (millis() - lastPrint >= 1000) {
      float seconds = totalBytes / (float)(SAMPLE_RATE * 2);
      float kb = totalBytes / 1024.0;
      int bars = map(peakLevel, 0, 32767, 0, 40);
      
      Serial.printf("   %.1fs (%.1fKB) [", seconds, kb);
      for (int i = 0; i < 40; i++) {
        Serial.print(i < bars ? "█" : "·");
      }
      Serial.printf("] %d\n", peakLevel);
      
      peakLevel = peakLevel * 0.5;
      lastPrint = millis();
    }
  }
}

void stopRecording() {
  if (!recording) return;
  recording = false;
  
  uint32_t fileSize = audioFile.size();
  uint32_t dataSize = fileSize - sizeof(WAV_HEADER);
  
  // Update WAV header
  audioFile.seek(4);
  uint32_t flength = fileSize - 8;
  audioFile.write((uint8_t*)&flength, 4);
  
  audioFile.seek(40);
  audioFile.write((uint8_t*)&dataSize, 4);
  
  audioFile.close();
  
  float duration = dataSize / (float)(SAMPLE_RATE * 2);
  Serial.printf("\n⏹ Stopped!\n");
  Serial.printf("   Duration: %.1fs\n", duration);
  Serial.printf("   Size: %.1fKB\n\n", fileSize / 1024.0);
}

void listFiles() {
  Serial.println("\n📁 Files on SD Card:");
  Serial.println("========================================");
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("❌ Cannot open directory");
    return;
  }
  
  int count = 0;
  float totalSize = 0;
  
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      float sizeKB = file.size() / 1024.0;
      Serial.printf("  %s - %.2f KB\n", file.name(), sizeKB);
      totalSize += sizeKB;
      count++;
    }
    file = root.openNextFile();
  }
  
  if (count == 0) {
    Serial.println("  (no files)");
  } else {
    Serial.printf("\n  Total: %d files (%.2f KB)\n", count, totalSize);
  }
  Serial.println("========================================\n");
}

void deleteAllAudioFiles() {
  Serial.println("\n🗑️  Deleting audio files...");
  
  File root = SD.open("/");
  if (!root) return;
  
  int deleted = 0;
  File file = root.openNextFile();
  
  while (file) {
    String filename = String(file.name());
    file.close();
    
    if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
      if (SD.remove(filename)) {
        Serial.printf("  ✓ Deleted: %s\n", filename.c_str());
        deleted++;
      }
    }
    file = root.openNextFile();
  }
  
  root.close();
  
  Serial.printf("%s\n\n", deleted ? "✓ Done" : "  No files to delete");
}

void testMicrophone() {
  Serial.println("\n🎤 Testing microphone for 10 seconds...");
  Serial.println("   SPEAK LOUDLY or CLAP!\n");
  Serial.printf("   Gain: %dx\n\n", currentGain);
  
  int16_t buffer[BUFFER_SIZE];
  size_t bytes_read = 0;
  
  i2s_zero_dma_buffer(I2S_PORT);
  delay(100);
  
  unsigned long startTime = millis();
  int16_t peakLevel = 0;
  
  while (millis() - startTime < 10000) {
    esp_err_t result = i2s_read(I2S_PORT, buffer, sizeof(buffer), &bytes_read, 100);
    
    if (result == ESP_OK && bytes_read > 0) {
      long sum = 0;
      int samples = bytes_read / sizeof(int16_t);
      int16_t maxInBuffer = 0;
      
      for (int i = 0; i < samples; i++) {
        int16_t sample = abs(buffer[i]) * currentGain;
        sum += sample;
        if (sample > maxInBuffer) maxInBuffer = sample;
      }
      
      int average = sum / samples;
      if (maxInBuffer > peakLevel) peakLevel = maxInBuffer;
      
      // Display bar graph
      int bars = map(average, 0, 5000, 0, 50);
      Serial.print("   Level: ");
      Serial.print(average);
      Serial.print(" |");
      for (int i = 0; i < bars; i++) Serial.print("=");
      for (int i = bars; i < 50; i++) Serial.print(" ");
      Serial.printf("| Peak: %d\r", peakLevel);
    }
    
    delay(100);
  }
  
  Serial.println("\n");
  Serial.printf("✓ Test complete! Peak: %d\n", peakLevel);
  
  if (peakLevel < 100) {
    Serial.println("❌ NO AUDIO! Check microphone.");
  } else if (peakLevel < 1000) {
    Serial.println("⚠ Low. Press '+' to increase gain.");
  } else if (peakLevel > 30000) {
    Serial.println("⚠ Too high! Press '-' to decrease gain.");
  } else {
    Serial.println("✓ Good level! Ready to record.");
  }
  Serial.println();
}
