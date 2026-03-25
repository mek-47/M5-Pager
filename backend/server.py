import paho.mqtt.client as mqtt

file = None

def on_connect(client, userdata, flags, reason_code, properties):
    print("Connected:", reason_code)
    client.subscribe("voice/m5_02/#")

def on_message(client, userdata, msg):
    global file

    topic = msg.topic

    if topic.endswith("/start"):
        print("Start receiving")
        file = open("output.raw", "wb")

    elif topic.endswith("/data"):
        if file:
            file.write(msg.payload)

    elif topic.endswith("/end"):
        if file:
            file.close()
            print("Saved output.raw")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

client.on_connect = on_connect
client.on_message = on_message

client.connect("broker.hivemq.com", 1883)

client.loop_forever()