import { Elysia } from "elysia";
import mqtt, { type MqttClient } from "mqtt";

// ===== MQTT CONFIG =====
const MQTT_BROKER =
  process.env.MQTT_BROKER || "bda50acbea674d6ab52f378c2e72b560.s1.eu.hivemq.cloud";
const MQTT_TRANSPORT =
  (process.env.MQTT_TRANSPORT || (typeof Bun !== "undefined" ? "mqtts" : "wss")).toLowerCase();
const MQTT_PORT = process.env.MQTT_PORT || "8883";
const MQTT_WS_PORT = process.env.MQTT_WEBSOCKET_PORT || "8884";
const MQTT_USER = process.env.MQTT_USER || "";
const MQTT_PASS = process.env.MQTT_PASS || "";

const useWebSocket = MQTT_TRANSPORT === "ws" || MQTT_TRANSPORT === "wss";
const protocol = useWebSocket ? "wss" : "mqtts";
const port = useWebSocket ? MQTT_WS_PORT : MQTT_PORT;
const mqttUrl = useWebSocket
  ? `${protocol}://${MQTT_BROKER}:${port}/mqtt`
  : `${protocol}://${MQTT_BROKER}:${port}`;

let client: MqttClient;

function connectMqtt(): MqttClient {
  if (client) return client;

  client = mqtt.connect(mqttUrl, {
    username: MQTT_USER,
    password: MQTT_PASS,
    protocol,
    rejectUnauthorized: false,
  });

  client.on("connect", () => {
    console.log(`[MQTT] Connected to broker via ${protocol.toUpperCase()}`);

    // Subscribe to topics the frontend needs
    client.subscribe("pump/status", (err) => {
      if (err) console.error("[MQTT] Subscribe error (pump/status):", err);
      else console.log("[MQTT] Subscribed to pump/status");
    });
  });

  client.on("error", (error) => {
    console.error("[MQTT] Connection error:", error);
  });

  client.on("reconnect", () => {
    console.log("[MQTT] Reconnecting...");
  });

  return client;
}

// Initialize MQTT connection on module load
connectMqtt();

// Track connected WebSocket clients
const wsClients = new Set<any>();

const mqttRoutes = new Elysia()

  // WebSocket bridge: forwards MQTT messages to frontend clients
  .ws("/ws/mqtt", {
    open(ws) {
      wsClients.add(ws);
      console.log(`[WS] Client connected (${wsClients.size} total)`);
    },

    message(ws, message) {
      // Frontend can send subscription requests via WebSocket
      // e.g. { action: "subscribe", topic: "pump/status" }
      try {
        const msg = typeof message === "string" ? JSON.parse(message) : message;

        if (msg && typeof msg === "object" && "action" in msg) {
          if (msg.action === "subscribe" && msg.topic) {
            client.subscribe(msg.topic as string, (err) => {
              if (err) {
                ws.send(JSON.stringify({ error: `Failed to subscribe to ${msg.topic}` }));
              } else {
                ws.send(JSON.stringify({ subscribed: msg.topic }));
              }
            });
          }
        }
      } catch {
        // Ignore malformed messages
      }
    },

    close(ws) {
      wsClients.delete(ws);
      console.log(`[WS] Client disconnected (${wsClients.size} total)`);
    },
  })

  // POST /api/mqtt/publish — Publish a message to an MQTT topic
  .post("/api/mqtt/publish", async ({ body, set }) => {
    const { topic, message } = body as { topic: string; message: string };

    if (!topic || message === undefined) {
      set.status = 400;
      return { error: "topic and message are required" };
    }

    return new Promise((resolve) => {
      client.publish(topic, message, (err) => {
        if (err) {
          set.status = 500;
          resolve({ error: "Failed to publish", details: err.message });
        } else {
          resolve({ success: true, topic, message });
        }
      });
    });
  });

// Forward all incoming MQTT messages to connected WebSocket clients
connectMqtt().on("message", (topic: string, payload: Buffer) => {
  const data = JSON.stringify({
    topic,
    payload: payload.toString(),
    timestamp: Date.now(),
  });

  for (const ws of wsClients) {
    try {
      ws.send(data);
    } catch {
      wsClients.delete(ws);
    }
  }
});

export default mqttRoutes;