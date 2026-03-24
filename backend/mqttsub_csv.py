import paho.mqtt.client as mqtt
import json
import csv
import os

# MQTT CONFIG
BROKER = "138.68.176.20"
PORT = 1883
TOPIC = "/jsonTopic"

# CSV CONFIG 
CSV_FILE = "imu_log.csv"
HEADER = [
    "No",
    "gyroX", "gyroY", "gyroZ",
    "accX", "accY", "accZ",
    "roll", "pitch", "yaw"
]

# INIT CSV 
if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(HEADER)

row_count = 0

# MQTT CALLBACKS
def on_connect(client, userdata, flags, rc):
    print("Connected with result code", rc)
    client.subscribe(TOPIC)

def on_message(client, userdata, msg):
    global row_count

    print("Message received ->", msg.payload)

    # Decode JSON
    data = json.loads(msg.payload.decode("utf-8"))

    # Prepare CSV row
    row = [
        row_count,
        data["gyro"]["x"],
        data["gyro"]["y"],
        data["gyro"]["z"],
        data["acc"]["x"],
        data["acc"]["y"],
        data["acc"]["z"],
        data["angle"]["roll"],
        data["angle"]["pitch"],
        data["angle"]["yaw"]
    ]

    # Append to CSV
    with open(CSV_FILE, "a", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(row)

    print("Saved row:", row_count)
    row_count += 1

# MQTT CLIENT
client = mqtt.Client(
    client_id="csvLogger",
    callback_api_version=mqtt.CallbackAPIVersion.VERSION1
)

client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, 60)
client.loop_forever()

