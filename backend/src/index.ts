import { Elysia } from "elysia";
import MqttSock from "./routes/MqttSock";

const app = new Elysia()
  .get("/", () => "Hello Elysia").listen(3000);

console.log(
  `🦊 Elysia is running at ${app.server?.hostname}:${app.server?.port}`
);
