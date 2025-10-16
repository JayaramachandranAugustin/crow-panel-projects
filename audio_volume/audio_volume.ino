#include <driver/i2s.h>

// I2S Microphone Pins for CrowPanel Advance 2.8"
#define I2S_MIC_SD    10    // Data pin
#define I2S_MIC_WS    3     // Word Select (LRCLK)
#define I2S_MIC_CLK   9     // Bit Clock (BCLK)
#define MIC_ENABLE    45    // Set HIGH to enable MIC, LOW for wireless module

// I2S Configuration
#define I2S_PORT      I2S_NUM_0
#define SAMPLE_RATE   16000
#define BUFFER_SIZE   512

void setup() {
  Serial.begin(115200);
  Serial.println("CrowPanel 2.8 Microphone Test");
  
  // Enable the microphone (set IO45 HIGH)
  pinMode(MIC_ENABLE, OUTPUT);
  digitalWrite(MIC_ENABLE, HIGH);
  delay(100);
  
  // Configure I2S for microphone input
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  // I2S pin configuration
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_CLK,      // Bit Clock
    .ws_io_num = I2S_MIC_WS,        // Word Select
    .data_out_num = I2S_PIN_NO_CHANGE,  // Not used for input
    .data_in_num = I2S_MIC_SD       // Data In
  };
  
  // Install and start I2S driver
  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install I2S driver: %d\n", err);
    return;
  }
  
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S pins: %d\n", err);
    return;
  }
  
  Serial.println("I2S Microphone initialized successfully!");
}

void loop() {
  int16_t buffer[BUFFER_SIZE];
  size_t bytes_read = 0;
  
  // Read audio data from microphone
  esp_err_t result = i2s_read(I2S_PORT, buffer, sizeof(buffer), &bytes_read, portMAX_DELAY);
  
  if (result == ESP_OK && bytes_read > 0) {
    // Calculate audio level (simple RMS)
    long sum = 0;
    int samples = bytes_read / sizeof(int16_t);
    
    for (int i = 0; i < samples; i++) {
      sum += abs(buffer[i]);
    }
    
    int average = sum / samples;
    
    // Print audio level as a simple bar graph
    Serial.print("Audio Level: ");
    Serial.print(average);
    Serial.print(" |");
    
    int bars = map(average, 0, 5000, 0, 50);
    for (int i = 0; i < bars; i++) {
      Serial.print("=");
    }
    Serial.println("|");
    
    // Optional: Print raw sample data (commented out to reduce serial output)
    // for (int i = 0; i < 10; i++) {
    //   Serial.printf("%d ", buffer[i]);
    // }
    // Serial.println();
  }
  
  delay(100);
}