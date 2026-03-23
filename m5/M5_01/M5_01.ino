#include <M5CoreS3.h>
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPIFFS.h>

// ===== WIFI =====
#define WIFI_SSID "Atom 2.4G_plus_plus"
#define WIFI_PASS "3512911674"

// ===== MQTT =====
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT 1883

#define DEVICE_ID "m5_01"
#define TARGET_ID "m5_02"

WiFiClient espClient;
PubSubClient client(espClient);

// ===== STATE =====
bool isRecording = false;
unsigned long recordStart = 0;

File recFile;
File rxFile;

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
  M5.Display.drawString("Voice Pager FIX", 160, 20);

  drawButton(btnRec);
  drawButton(btnPlay);
}

// ===== MQTT =====
void callback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);

  if (t.endsWith("/start")) {
    SPIFFS.remove("/recv.raw");
    rxFile = SPIFFS.open("/recv.raw", FILE_WRITE);
  }
  else if (t.endsWith("/data")) {
    if (rxFile) rxFile.write(payload, length);
  }
  else if (t.endsWith("/end")) {
    if (rxFile) rxFile.close();
  }
}

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
  if (!file) return;

  String base = "voice/" + String(TARGET_ID);

  client.publish((base + "/start").c_str(), "1");

  uint8_t chunk[512];

  while (file.available()) {
    int len = file.read(chunk, 512);
    client.publish((base + "/data").c_str(), chunk, len);
    client.loop();
    delay(2);
  }

  client.publish((base + "/end").c_str(), "1");
  file.close();
}

// ===== PLAY (FIXED) =====
void playAudio() {
  M5.Speaker.stop();

  File file = SPIFFS.open("/recv.raw");
  if (!file) {
    Serial.println("No audio file");
    return;
  }

  uint8_t buf[512];

  while (file.available()) {
    int len = file.read(buf, 512);

    // 🔊 RAW PCM PLAY (FIXED)
    M5.Speaker.playRaw(buf, len, 16000);
    delay(1);
  }

  file.close();
}

// ===== SETUP =====
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);

  SPIFFS.begin(true);

  // 🔊 Speaker init FIX
  M5.Speaker.begin();
  M5.Speaker.setVolume(200);

  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  Serial.println(WiFi.localIP());

  // MQTT
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callback);

  drawUI();
}

// ===== LOOP =====
void loop() {
  M5.update();

  if (!client.connected()) reconnect();
  client.loop();

  auto t = M5.Touch.getDetail();

  // ===== REC START =====
  if (!isRecording && t.isPressed() && isInside(btnRec, t.x, t.y)) {
    SPIFFS.remove("/record.raw");
    recFile = SPIFFS.open("/record.raw", FILE_WRITE);

    isRecording = true;
    recordStart = millis();

    drawButton(btnRec, true);
  }

  // ===== RECORD =====
  if (isRecording) {
    int16_t buffer[256];
    size_t len = M5.Mic.record(buffer, 256);

    recFile.write((uint8_t*)buffer, len);

    float sec = (millis() - recordStart) / 1000.0;

    M5.Display.fillRect(0, 80, 320, 40, BLACK);
    M5.Display.setCursor(100, 90);
    M5.Display.printf("REC: %.1fs", sec);

    delay(1);
  }

  // ===== STOP =====
  if (isRecording && !t.isPressed()) {
    recFile.close();
    isRecording = false;

    sendFile();
    drawUI();
  }

  // ===== PLAY =====
  static unsigned long lastPlay = 0;

  if (t.wasPressed() && isInside(btnPlay, t.x, t.y)) {
    if (millis() - lastPlay < 500) return;
    lastPlay = millis();

    drawButton(btnPlay, true);

    playAudio();

    drawUI();
  }
}