import { Elysia } from "elysia";
import { cors } from "@elysiajs/cors";
import audioRoutes from "./routes/audio";
import mqttRoutes from "./routes/MqttSock";

const app = new Elysia()
  .use(
    cors({
      origin: process.env.CORS_ORIGIN || "http://localhost:5173",
      methods: ["GET", "POST", "DELETE", "OPTIONS"],
    })
  )
  .use(audioRoutes)
  .use(mqttRoutes)
  .get("/", () => ({ status: "ok", service: "m5-pager-backend" }))
  .listen(3000);

console.log(
  `🦊 Elysia is running at ${app.server?.hostname}:${app.server?.port}`
);
