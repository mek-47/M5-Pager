#include <M5CoreS3.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SPIFFS.h>

// ================= WIFI =================
#define WIFI_SSID "Atom 2.4G_plus_plus"
#define WIFI_PASS "3512911674"

// ================= MQTT =================
#define MQTT_BROKER "9291c430d3b1463385026d1c706457d8.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_USER "testm5"
#define MQTT_PASS "Wolf0492"

// ================= DEVICE =================
#define DEVICE_ID "m5_01"

// ================= AUDIO =================
#define SAMPLE_RATE 16000
#define FRAME_LEN 256

WiFiClientSecure net;
PubSubClient client(net);

// ================= STATE =================
File recFile;
File rxFile;

bool isRecording = false;
bool newMessage = false;
String senderId = "";
unsigned long lastTouch = 0;

// ================= BUTTON =================
struct Button {
  int x,y,w,h;
  const char* label;
  uint16_t color;
};

Button btnRec  = {40,160,120,70,"REC",RED};
Button btnPlay = {200,160,120,70,"PLAY",BLUE};

bool hit(Button b,int x,int y){
  return (x>b.x && x<b.x+b.w && y>b.y && y<b.y+b.h);
}

void drawBtn(Button b){
  M5.Display.fillRoundRect(b.x,b.y,b.w,b.h,15,b.color);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(WHITE);
  M5.Display.drawString(b.label,b.x+b.w/2,b.y+b.h/2);
}

// ================= MQTT CONNECT =================
void reconnect() {
  while (!client.connected()) {

    if (client.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) {

      client.subscribe("v/+/s");
      client.subscribe("v/+/d");
      client.subscribe("v/+/e");

    } else {
      delay(1000);
    }
  }
}

// ================= AUDIO PROCESS =================
void processAudio(int16_t* buffer) {

  long sum = 0;
  for (int i = 0; i < FRAME_LEN; i++) sum += buffer[i];
  int offset = sum / FRAME_LEN;

  int16_t prev = 0;

  for (int i = 0; i < FRAME_LEN; i++) {

    int v = buffer[i] - offset;

    v = v - (int)(0.9 * prev);
    prev = v;

    v = v * 2.5;

    if (abs(v) < 120) v = 0;

    if (v > 30000) v = 30000;
    if (v < -30000) v = -30000;

    buffer[i] = v;
  }
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int len) {

  String msg = "";
  for (int i = 0; i < len; i++) msg += (char)payload[i];

  // ===== parse sender id =====
  String sender = "";

  int idIndex = msg.indexOf("\"id\":\"");
  if (idIndex != -1) {
    sender = msg.substring(idIndex + 6);
    sender = sender.substring(0, sender.indexOf("\""));
  }

  // ❌ IGNORE SELF MESSAGE (IMPORTANT FIX)
  if (sender == DEVICE_ID) return;

  // ================= START =================
  if (String(topic).endsWith("/s")) {

    newMessage = true;
    senderId = sender;

    SPIFFS.remove("/recv.raw");
    rxFile = SPIFFS.open("/recv.raw", FILE_WRITE);

    M5.Speaker.tone(2000, 80);
    delay(80);
    M5.Speaker.tone(2600, 80);
  }

  // ================= DATA =================
  else if (String(topic).endsWith("/d")) {
    if (rxFile) rxFile.write(payload, len);
  }

  // ================= END =================
  else if (String(topic).endsWith("/e")) {
    if (rxFile) rxFile.close();

    M5.Speaker.tone(3000, 120);
  }
}

// ================= SEND =================
void sendFile() {

  File file = SPIFFS.open("/record.raw");
  if (!file) return;

  int16_t buffer[FRAME_LEN];

  // 🔥 send START with JSON
  String startMsg = "{\"id\":\"" + String(DEVICE_ID) + "\"}";
  client.publish(("v/" + String(DEVICE_ID) + "/s").c_str(), startMsg.c_str());

  while (file.available()) {

    file.read((uint8_t*)buffer, FRAME_LEN * 2);

    client.publish(
      ("v/" + String(DEVICE_ID) + "/d").c_str(),
      (uint8_t*)buffer,
      FRAME_LEN * 2,
      false
    );

    client.loop();
    delay(6);
  }

  client.publish(("v/" + String(DEVICE_ID) + "/e").c_str(), startMsg.c_str());

  file.close();
}

// ================= PLAY =================
void playAudio(const char* path) {

  File f = SPIFFS.open(path);
  if (!f) return;

  int16_t buf[FRAME_LEN];

  while (f.available()) {
    int len = f.read((uint8_t*)buf, sizeof(buf));
    M5.Speaker.playRaw(buf, len / 2, SAMPLE_RATE);
  }

  f.close();
}

// ================= UI =================
void drawNotification() {

  if (!newMessage) return;

  M5.Display.fillRect(0, 0, 320, 40, RED);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("NEW MESSAGE", 160, 10);
  M5.Display.drawString(senderId, 160, 28);
}

// ================= SETUP =================
void setup() {

  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);

  // MIC
  auto mic_cfg = M5.Mic.config();
  mic_cfg.sample_rate = SAMPLE_RATE;
  M5.Mic.config(mic_cfg);
  M5.Mic.begin();

  // SPEAKER
  M5.Speaker.begin();
  M5.Speaker.setVolume(255);

  SPIFFS.begin(true);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  net.setInsecure();

  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callback);
  client.setBufferSize(4096);

  M5.Display.fillScreen(BLACK);
  drawBtn(btnRec);
  drawBtn(btnPlay);
}

// ================= LOOP =================
void loop() {

  M5.update();

  if (!client.connected()) reconnect();
  client.loop();

  auto t = M5.Touch.getDetail();

  // ================= RECORD =================
  if (!isRecording && t.isPressed() && hit(btnRec, t.x, t.y)) {

    SPIFFS.remove("/record.raw");
    recFile = SPIFFS.open("/record.raw", FILE_WRITE);

    isRecording = true;
    lastTouch = millis();
  }

  if (isRecording) {

    int16_t buffer[FRAME_LEN];

    if (M5.Mic.record(buffer, FRAME_LEN)) {
      processAudio(buffer);
      recFile.write((uint8_t*)buffer, FRAME_LEN * 2);
    }

    if (t.isPressed()) lastTouch = millis();
  }

  // STOP + SEND
  if (isRecording && millis() - lastTouch > 400) {

    recFile.close();
    isRecording = false;

    sendFile();
  }

  // ================= PLAY =================
  if (t.wasPressed() && hit(btnPlay, t.x, t.y)) {

    newMessage = false;

    if (SPIFFS.exists("/recv.raw")) {
      playAudio("/recv.raw");
    }
  }

  drawNotification();
}