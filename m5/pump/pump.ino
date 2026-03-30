#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <DHT.h>

// ===== WIFI =====
#define WIFI_SSID "Atom 2.4G_plus_plus"
#define WIFI_PASS "3512911674"

// ===== MQTT =====
#define MQTT_BROKER "9291c430d3b1463385026d1c706457d8.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883

#define MQTT_USER "testm5"
#define MQTT_PASS "Wolf0492"

#define DEVICE_ID "esp32c3_01"

// DHT11 pin GPI02
#define DHT_PIN 2
#define DHT_TYPE DHT11
DHT dht(DHT_PIN, DHT_TYPE);

// PUMP pin GPI10 MOSI D10
#define PUMP_PIN 10

// ===== AUTO CONTROL =====
float TEMP_THRESHOLD = 30.0;  // เกินนี้เปิดปั๊ม
bool autoMode = true;         // default = AUTO

WiFiClientSecure espClient;
PubSubClient client(espClient);

// ===== CALLBACK =====
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  String t = String(topic);

  Serial.print("Topic: ");
  Serial.print(t);
  Serial.print(" | Msg: ");
  Serial.println(msg);

  // ===== MODE CONTROL =====
  if (t == "pump/c3/mode") {
    if (msg == "AUTO") {
      autoMode = true;
      Serial.println("Mode = AUTO");
    } else if (msg == "MANUAL") {
      autoMode = false;
      Serial.println("Mode = MANUAL");
    }
  }

  // ===== MANUAL CONTROL =====
  if (t == "pump/c3/cmd" && !autoMode) {
    if (msg == "ON") {
      digitalWrite(PUMP_PIN, HIGH);
      Serial.println("Pump ON (Manual)");
    }
    else if (msg == "OFF") {
      digitalWrite(PUMP_PIN, LOW);
      Serial.println("Pump OFF (Manual)");
    }
  }
}

// ===== RECONNECT =====
void reconnect() {
  while (!client.connected()) {
    Serial.print("MQTT connecting...");

    if (client.connect(DEVICE_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("OK");

      client.subscribe("pump/c3/cmd");
      client.subscribe("pump/c3/mode");

    } else {
      Serial.print("FAILED rc=");
      Serial.println(client.state());
      delay(1000);
    }
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  dht.begin();

  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");

  // TLS
  espClient.setInsecure();

  // MQTT
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callback);
}

// ===== LOOP =====
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  static unsigned long lastSend = 0;

  if (millis() - lastSend > 5000) {
    lastSend = millis();

    float temp = dht.readTemperature();

    if (!isnan(temp)) {
      char msg[20];
      dtostrf(temp, 4, 2, msg);

      client.publish("sensor/c3/temp", msg);

      Serial.print("Temp: ");
      Serial.println(msg);

      // ===== AUTO CONTROL =====
      if (autoMode) {
        if (temp > TEMP_THRESHOLD) {
          digitalWrite(PUMP_PIN, HIGH);
          Serial.println("Pump ON (AUTO)");
        } else {
          digitalWrite(PUMP_PIN, LOW);
          Serial.println("Pump OFF (AUTO)");
        }
      }

    } else {
      Serial.println("DHT ERROR");
    }
  }

  delay(10);
}