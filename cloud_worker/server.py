import os
import io
import ssl
import wave
import time
import uuid
import psycopg2
import paho.mqtt.client as mqtt


def env_bool(name: str, default: bool) -> bool:
    """Parse a boolean env var with a safe default."""
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() in {"1", "true", "yes", "on"}


# ===== CONFIG =====
MQTT_BROKER = os.environ.get("MQTT_BROKER", "bda50acbea674d6ab52f378c2e72b560.s1.eu.hivemq.cloud")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "8883"))
MQTT_USER = os.environ.get("MQTT_USER", "")
MQTT_PASS = os.environ.get("MQTT_PASS", "")
DATABASE_URL = os.environ.get("DATABASE_URL", "")
AUDIO_DIR = os.environ.get("AUDIO_DIR", "/audio")

# ===== AUDIO SPECS (from M5 firmware) =====
PCM_SAMPLE_RATE = int(os.environ.get("PCM_SAMPLE_RATE", "16000"))
WAV_RATE_FACTOR = float(os.environ.get("WAV_RATE_FACTOR", "1.0"))
WAV_SAMPLE_RATE = max(1, int(round(PCM_SAMPLE_RATE * WAV_RATE_FACTOR)))
CHANNELS = int(os.environ.get("PCM_CHANNELS", "1"))
SAMPLE_WIDTH = int(os.environ.get("PCM_SAMPLE_WIDTH", "2"))  # 16-bit signed = 2 bytes
PAD_TO_EXPECTED_BYTES = env_bool("PAD_TO_EXPECTED_BYTES", True)

# ===== IN-MEMORY BUFFERS =====
# Tracks concurrent recordings from multiple devices
# { device_id: { "buffer": bytearray, "expected_bytes": int } }
active_recordings: dict[str, dict] = {}


def get_db_connection():
    """Create a new database connection."""
    return psycopg2.connect(DATABASE_URL)


def ensure_audio_dir():
    """Create the audio output directory if it doesn't exist."""
    os.makedirs(AUDIO_DIR, exist_ok=True)


def normalize_raw_audio(raw_data: bytes, expected_bytes: int = 0) -> bytes:
    """Normalize PCM bytes for WAV conversion.

    - Optionally pad to expected bytes to preserve expected duration on packet loss
    - Trim to full PCM sample boundaries
    """
    normalized = raw_data

    if expected_bytes > 0:
        expected_aligned = expected_bytes - (expected_bytes % SAMPLE_WIDTH)
        if expected_aligned != expected_bytes:
            print(
                f"[AUDIO] expected_bytes {expected_bytes} is not sample-aligned; using {expected_aligned}"
            )
        expected_bytes = expected_aligned

        if len(normalized) < expected_bytes and PAD_TO_EXPECTED_BYTES:
            missing = expected_bytes - len(normalized)
            normalized += b"\x00" * missing
            print(f"[AUDIO] Padded {missing} missing bytes with silence")
        elif len(normalized) > expected_bytes:
            extra = len(normalized) - expected_bytes
            normalized = normalized[:expected_bytes]
            print(f"[AUDIO] Trimmed {extra} extra bytes beyond expected size")

    remainder = len(normalized) % SAMPLE_WIDTH
    if remainder:
        normalized = normalized[:-remainder]
        print(f"[AUDIO] Dropped {remainder} trailing byte(s) to keep PCM alignment")

    return normalized


def raw_to_wav(raw_data: bytes, sample_rate_hz: int) -> bytes:
    """Convert raw PCM data to WAV format in memory."""
    wav_buffer = io.BytesIO()
    with wave.open(wav_buffer, "wb") as wf:
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(SAMPLE_WIDTH)
        wf.setframerate(sample_rate_hz)
        wf.writeframes(raw_data)
    return wav_buffer.getvalue()


def calculate_duration_ms(raw_bytes: int, sample_rate_hz: int) -> int:
    """Calculate audio duration in ms from raw byte count.

    Formula: samples / sample_rate * 1000
    where samples = raw_bytes / (sample_width * channels)
    """
    frame_width = SAMPLE_WIDTH * CHANNELS
    if frame_width <= 0 or sample_rate_hz <= 0:
        return 0

    samples = raw_bytes / frame_width
    duration_sec = samples / sample_rate_hz
    return int(round(duration_sec * 1000))


def save_and_record(device_id: str, raw_data: bytearray, expected_bytes: int = 0):
    """Convert raw audio to WAV, save to disk, and INSERT into database."""
    timestamp = int(time.time() * 1000)
    filename = f"{device_id}_{timestamp}.wav"
    file_path = os.path.join(AUDIO_DIR, filename)

    normalized_raw = normalize_raw_audio(bytes(raw_data), expected_bytes)

    # Convert raw → WAV
    wav_data = raw_to_wav(normalized_raw, WAV_SAMPLE_RATE)

    # Save WAV file to shared volume
    with open(file_path, "wb") as f:
        f.write(wav_data)

    duration_ms = calculate_duration_ms(len(normalized_raw), WAV_SAMPLE_RATE)
    file_size = len(wav_data)

    print(
        f"[SAVE] {filename} — {file_size} bytes, {duration_ms}ms duration "
        f"(raw={len(raw_data)}B normalized={len(normalized_raw)}B wav_rate={WAV_SAMPLE_RATE}Hz)"
    )

    # INSERT record into PostgreSQL
    try:
        conn = get_db_connection()
        cur = conn.cursor()
        cur.execute(
            """
            INSERT INTO audio_records (id, device_id, filename, file_path, duration_ms, sample_rate, file_size, created_at)
            VALUES (%s, %s, %s, %s, %s, %s, %s, NOW())
            """,
            (
                str(uuid.uuid4()),
                device_id,
                filename,
                f"/audio/{filename}",
                duration_ms,
                WAV_SAMPLE_RATE,
                file_size,
            ),
        )
        conn.commit()
        cur.close()
        conn.close()
        print(f"[DB] Inserted record for {filename}")
    except Exception as e:
        print(f"[DB] ERROR inserting record: {e}")


# ===== MQTT CALLBACKS =====
def on_connect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Connected: {reason_code}")
    client.subscribe("voice/#")
    print("[MQTT] Subscribed to voice/#")


def on_message(client, userdata, msg):
    topic = msg.topic
    parts = topic.split("/")

    # Expected format: voice/{device_id}/{action}
    if len(parts) != 3 or parts[0] != "voice":
        return

    device_id = parts[1]
    action = parts[2]

    if action == "start":
        # Parse expected total byte count from payload
        try:
            expected_bytes = int(msg.payload.decode("utf-8").strip())
        except (ValueError, UnicodeDecodeError):
            expected_bytes = 0

        active_recordings[device_id] = {
            "buffer": bytearray(),
            "expected_bytes": expected_bytes,
        }
        print(f"[RX] {device_id}: Recording started, expecting {expected_bytes} bytes")

    elif action == "data":
        if device_id in active_recordings:
            active_recordings[device_id]["buffer"].extend(msg.payload)

    elif action == "end":
        if device_id in active_recordings:
            recording = active_recordings.pop(device_id)
            received = len(recording["buffer"])
            expected = recording["expected_bytes"]

            if expected > 0 and received != expected:
                print(f"[RX] {device_id}: WARNING — expected {expected} bytes, got {received} bytes")
            else:
                print(f"[RX] {device_id}: Transfer complete — {received} bytes")

            if received > 0:
                save_and_record(device_id, recording["buffer"], expected)
            else:
                print(f"[RX] {device_id}: Empty recording, skipping")


def on_disconnect(client, userdata, flags, reason_code, properties):
    print(f"[MQTT] Disconnected: {reason_code}")


# ===== MAIN =====
if __name__ == "__main__":
    ensure_audio_dir()

    print(f"[INIT] MQTT Broker: {MQTT_BROKER}:{MQTT_PORT}")
    print(f"[INIT] Audio dir: {AUDIO_DIR}")
    print(
        f"[INIT] Audio specs: pcm={PCM_SAMPLE_RATE}Hz, wav={WAV_SAMPLE_RATE}Hz "
        f"(factor={WAV_RATE_FACTOR}), {SAMPLE_WIDTH * 8}-bit, {CHANNELS}ch"
    )

    # Wait for database to be ready
    for attempt in range(10):
        try:
            conn = get_db_connection()
            conn.close()
            print("[INIT] Database connection OK")
            break
        except Exception as e:
            print(f"[INIT] Waiting for database... (attempt {attempt + 1}/10)")
            time.sleep(3)
    else:
        print("[INIT] WARNING: Could not connect to database, will retry on first write")

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)

    # TLS for HiveMQ Cloud
    client.tls_set(tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.tls_insecure_set(True)
    client.username_pw_set(MQTT_USER, MQTT_PASS)

    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect

    print("[MQTT] Connecting...")
    client.connect(MQTT_BROKER, MQTT_PORT)

    client.loop_forever()