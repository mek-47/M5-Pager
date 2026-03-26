#include <M5CoreS3.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>

// ===== WIFI =====
#define WIFI_SSID "Atom 2.4G_plus_plus"
#define WIFI_PASS "3512911674"

// ===== MQTT =====
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883

#define DEVICE_ID "m5_02"
#define TARGET_ID "m5_01"

// ===== AUDIO CONFIG =====
#define SAMPLE_RATE    16000
#define RECORD_SAMPLES 256   // samples per record() call (int16_t each)

WiFiClient espClient;
PubSubClient client(espClient);

// ===== STATE =====
bool isRecording = false;
unsigned long recordStart = 0;

File recFile;
File rxFile;

// ===== RECEIVE INTEGRITY TRACKING =====
uint32_t expectedBytes = 0;
uint32_t receivedBytes = 0;

// ===== UI =====
struct Button {
  int x, y, w, h;
  const char* label;
  uint16_t color;
};

Button btnRec  = {40, 160, 120, 70, "REC",  RED};
Button btnPlay = {200, 160, 120, 70, "PLAY", BLUE};

// ===== DRAW =====
void drawButton(Button btn, bool pressed = false) {
  uint16_t c = pressed ? DARKGREY : btn.color;

  M5.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 15, c);
  M5.Display.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 15, WHITE);

  M5.Display.setTextColor(WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(btn.label, btn.x + btn.w/2, btn.y + btn.h/2);
}

bool isInside(Button btn, int tx, int ty) {
  return (tx > btn.x && tx < btn.x + btn.w &&
          ty > btn.y && ty < btn.y + btn.h);
}

void drawUI() {
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(top_center);
  M5.Display.setTextColor(WHITE);
  M5.Display.drawString("Voice Pager", 160, 20);

  drawButton(btnRec);
  drawButton(btnPlay);
}

void drawStatus(const char* msg, uint16_t color = WHITE) {
  M5.Display.fillRect(0, 80, 320, 60, BLACK);
  M5.Display.setCursor(20, 90);
  M5.Display.setTextColor(color);
  M5.Display.setTextSize(2);
  M5.Display.print(msg);
}

// ===== RELIABLE PUBLISH =====
// Retries up to 5 times with increasing backoff on failure
bool reliablePublish(const char* topic, const uint8_t* data, unsigned int len) {
  for (int attempt = 0; attempt < 5; attempt++) {
    if (client.publish(topic, data, len)) return true;
    delay(10 * (attempt + 1));  // 10, 20, 30, 40, 50ms backoff
    client.loop();
  }
  Serial.printf("[MQTT] publish FAILED after 5 attempts (topic: %s, len: %u)\n", topic, len);
  return false;
}

// Overload functino for string payloads
bool reliablePublish(const char* topic, const char* payload) {
  return reliablePublish(topic, (const uint8_t*)payload, strlen(payload));
}

// ===== MQTT CALLBACK =====
void callback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);

  if (t.endsWith("/start")) {
    // Parse expected total byte count from payload
    char tmp[16];
    unsigned int copyLen = (length < 15) ? length : 15;
    memcpy(tmp, payload, copyLen);
    tmp[copyLen] = '\0';
    expectedBytes = atol(tmp);
    receivedBytes = 0;

    SPIFFS.remove("/recv.raw");
    rxFile = SPIFFS.open("/recv.raw", FILE_WRITE);

    Serial.printf("[RX] Transfer start, expecting %u bytes\n", expectedBytes);
    drawStatus("Receiving...", CYAN);
  }
  else if (t.endsWith("/data")) {
    if (rxFile) {
      rxFile.write(payload, length);
      receivedBytes += length;
    }
  }
  else if (t.endsWith("/end")) {
    if (rxFile) {
      rxFile.close();

      // Verify integrity: does received byte count match expected?
      if (expectedBytes > 0 && receivedBytes != expectedBytes) {
        Serial.printf("[RX] WARNING: expected %u bytes, got %u bytes\n",
                      expectedBytes, receivedBytes);

        char msg[40];
        snprintf(msg, sizeof(msg), "WARN: %u/%u bytes", receivedBytes, expectedBytes);
        drawStatus(msg, YELLOW);
      } else {
        Serial.printf("[RX] OK: %u bytes received\n", receivedBytes);
        drawStatus("Received!", GREEN);
      }
    }
  }
}

// ===== MQTT RECONNECT =====
void reconnect() {
  while (!client.connected()) {
    Serial.print("MQTT connecting...");

    if (client.connect(DEVICE_ID)) {
      Serial.println("OK");

      String base = "voice/" + String(DEVICE_ID);
      client.subscribe((base + "/start").c_str());
      client.subscribe((base + "/data").c_str());
      client.subscribe((base + "/end").c_str());

    } else {
      Serial.print("FAILED rc=");
      Serial.println(client.state());
      delay(1000);
    }
  }
}

// ===== SEND =====
void sendFile() {
  File file = SPIFFS.open("/record.raw");
  if (!file) {
    drawStatus("No recording!", RED);
    return;
  }

  String base = "voice/" + String(TARGET_ID);

  // Send total file size in /start so receiver can verify integrity
  uint32_t totalSize = file.size();
  char sizeStr[12];
  ltoa(totalSize, sizeStr, 10);
  reliablePublish((base + "/start").c_str(), sizeStr);

  Serial.printf("[TX] Sending %u bytes...\n", totalSize);
  drawStatus("Sending...", CYAN);

  uint8_t chunk[512];
  int failCount = 0;

  while (file.available()) {
    int len = file.read(chunk, 512);

    if (!reliablePublish((base + "/data").c_str(), chunk, len)) {
      failCount++;
      Serial.printf("[TX] Chunk FAILED! Total failures: %d\n", failCount);
    }

    client.loop();
    delay(5);  // WiFi radio transmit compensation
  }

  reliablePublish((base + "/end").c_str(), "1");
  file.close();

  if (failCount > 0) {
    char msg[32];
    snprintf(msg, sizeof(msg), "Sent (%d fails)", failCount);
    drawStatus(msg, YELLOW);
  } else {
    drawStatus("Sent!", GREEN);
    Serial.printf("[TX] Done, %u bytes sent\n", totalSize);
  }
}

// ===== PLAY =====
void playAudio() {
  File file = SPIFFS.open("/recv.raw");
  if (!file) {
    drawStatus("No file!", RED);
    return;
  }

  // Switch I2S bus from mic to speaker
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);

  drawStatus("Playing...", GREEN);

  int16_t buf[256];

  while (file.available()) {
    int bytesRead = file.read((uint8_t*)buf, sizeof(buf));
    int sampleCount = bytesRead / sizeof(int16_t);

    M5.Speaker.playRaw(buf, sampleCount, SAMPLE_RATE);

   // Wait for speaker to finish before feeding next chunk
    while (M5.Speaker.isPlaying()) { delay(1); }
  }

  file.close();
}

// ===== SETUP =====
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);

  M5.Mic.begin();
  M5.Speaker.begin();
  M5.Speaker.setVolume(180);

  if (!SPIFFS.begin(true)) {
    M5.Display.println("SPIFFS FAIL");
    while (1);
  }

  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  M5.Display.println("Connecting WiFi...");
  while (WiFi.status() != WL_CONNECTED) delay(300);

  Serial.println(WiFi.localIP());

  // MQTT
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setBufferSize(1024);   // default 256 is too small for 512-byte chunks
  client.setCallback(callback);

  drawUI();
}

// ===== LOOP =====
void loop() {
  M5.update();

  if (!client.connected()) reconnect();
  client.loop();

  auto t = M5.Touch.getDetail();

  // ===== START RECORDING =====
  if (!isRecording && t.isPressed() && isInside(btnRec, t.x, t.y)) {
    // Switch I2S bus from speaker to mic
    M5.Speaker.end();
    M5.Mic.begin();

    SPIFFS.remove("/record.raw");
    recFile = SPIFFS.open("/record.raw", FILE_WRITE);

    isRecording = true;
    recordStart = millis();

    drawButton(btnRec, true);
  }

  // ===== RECORDING =====
  if (isRecording) {
    int16_t buffer[RECORD_SAMPLES];

    if (M5.Mic.record(buffer, RECORD_SAMPLES, SAMPLE_RATE)) {
    // Pass SAMPLE_RATE explicitly (default is 8000)
      recFile.write((uint8_t*)buffer, RECORD_SAMPLES * sizeof(int16_t)); // Write correct number of bytes: samples × 2 bytes each
    }

    float sec = (millis() - recordStart) / 1000.0;

    M5.Display.fillRect(0, 80, 320, 40, BLACK);
    M5.Display.setCursor(100, 90);
    M5.Display.setTextColor(WHITE);
    M5.Display.printf("REC: %.1fs", sec);
  }

  // ===== STOP RECORDING =====
  if (isRecording && !t.isPressed()) {
    recFile.close();
    isRecording = false;

    // Switch I2S bus back from mic to speaker
    M5.Mic.end();
    M5.Speaker.begin();
    M5.Speaker.setVolume(180);

    sendFile();
    drawUI();
  }

  // ===== PLAY =====
  if (t.wasPressed() && isInside(btnPlay, t.x, t.y)) {
    drawButton(btnPlay, true);

    playAudio();

    drawUI();
  }
}