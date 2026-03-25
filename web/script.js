const log = document.getElementById("log");

const client = mqtt.connect("ws://192.168.1.236:9001");

client.on("connect", () => {
  log.innerHTML = "Connected";
  client.subscribe("voice/#");
});

client.on("message", (topic, message) => {
  log.innerHTML = "Audio chunk: " + message.length;
});