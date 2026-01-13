/**
 * ESP32 I2S Audio Streamer Client
 * Hardware: ESP32-C3 SuperMini (or compatible) + INMP441 Microphone
 * Library Required: ArduinoWebsockets by Gil Maimon
 */

#include <driver/i2s.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>

// --- CONFIGURATION ---
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char *websocket_server_host = "192.168.1.100"; // Update with Python Server IP
const uint16_t websocket_server_port = 8888;

// --- HARDWARE PINS (ESP32-C3 SuperMini) ---
#define I2S_SCK 6
#define I2S_WS 3
#define I2S_SD 10
#define I2S_PORT I2S_NUM_0

// --- AUDIO SETTINGS ---
#define BUFFER_LEN 512
#define GAIN_FACTOR 64 // Software gain multiplier (adjust if too quiet/loud)

int16_t sBuffer[BUFFER_LEN];

using namespace websockets;
WebsocketsClient client;
bool isWebSocketConnected = false;

// --- CALLBACKS ---
void onMessageCallback(WebsocketsMessage message)
{
  // Handles text received from the Python server (Transcription)
  Serial.print("\n[SERVER RESPONSE]: ");
  Serial.println(message.data());

  // TODO: Add logic here to control hardware based on text
  // Example: if (message.data() == "lights on") digitalWrite(LED_PIN, HIGH);
}

void onEventsCallback(WebsocketsEvent event, String data)
{
  if (event == WebsocketsEvent::ConnectionOpened)
  {
    Serial.println("WebSocket Connected!");
    isWebSocketConnected = true;
  }
  else if (event == WebsocketsEvent::ConnectionClosed)
  {
    Serial.println("WebSocket Disconnected.");
    isWebSocketConnected = false;
  }
}

// --- I2S SETUP ---
void i2s_install()
{
  const i2s_config_t i2s_config = {
      .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = 16000, // Must match Python Server
      .bits_per_sample = i2s_bits_per_sample_t(16),
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = BUFFER_LEN,
      .use_apll = false};
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
}

void i2s_setpin()
{
  const i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SCK,
      .ws_io_num = I2S_WS,
      .data_out_num = -1,
      .data_in_num = I2S_SD};
  i2s_set_pin(I2S_PORT, &pin_config);
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

  // 1. Connect to WiFi
  WiFi.mode(WIFI_STA);
  // WiFi.setSleep(false); // Uncomment if audio stream is choppy/unstable
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");

  // 2. Setup WebSocket
  client.onEvent(onEventsCallback);
  client.onMessage(onMessageCallback);

  // 3. Connect to Server
  Serial.printf("Connecting to Server at %s:%d\n", websocket_server_host, websocket_server_port);
  while (!client.connect(websocket_server_host, websocket_server_port, "/"))
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nReady to stream.");

  // 4. Initialize Microphone
  i2s_install();
  i2s_setpin();
  i2s_start(I2S_PORT);
}

void loop()
{
  // Keep WebSocket connection alive
  if (client.available())
  {
    client.poll();
  }

  // Read Audio Data
  size_t bytesIn = 0;
  esp_err_t result = i2s_read(I2S_PORT, &sBuffer, BUFFER_LEN * sizeof(int16_t), &bytesIn, 5);

  if (result == ESP_OK && bytesIn > 0)
  {

    // Apply Software Gain
    // Note: Check for clipping if GAIN_FACTOR is too high
    for (int i = 0; i < bytesIn / 2; i++)
    {
      int32_t amplified = sBuffer[i] * GAIN_FACTOR;
      if (amplified > 32767)
        amplified = 32767;
      if (amplified < -32768)
        amplified = -32768;
      sBuffer[i] = (int16_t)amplified;
    }

    // Send to Python Server
    if (isWebSocketConnected)
    {
      client.sendBinary((const char *)sBuffer, bytesIn);
    }
  }
}