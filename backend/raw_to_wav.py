import wave

RAW_FILE = "audio.raw"
WAV_FILE = "audio.wav"

SAMPLE_RATE = 16000
CHANNELS = 1
SAMPLE_WIDTH = 2  # 16-bit

with open(RAW_FILE, "rb") as rf:
    raw_data = rf.read()

with wave.open(WAV_FILE, "wb") as wf:
    wf.setnchannels(CHANNELS)
    wf.setsampwidth(SAMPLE_WIDTH)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(raw_data)

print("Saved audio.wav")

