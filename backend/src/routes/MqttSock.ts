import Elysia from "elysia";
import mqtt from "mqtt";

let options = {
    host: process.env.MQTT_HOST,
}
let client = mqtt.connect(options);

client.on("connect", function() {
    console.log("Connected to MQTT broker");
})

client.on("error", function(error) {
    console.error("MQTT connection error:", error);
})

new Elysia()
    .get("mqtt/send", (ctx) => {
        
    })