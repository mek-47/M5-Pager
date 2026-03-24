import socket
import wave

UDP_IP = "0.0.0.0"
UDP_PORT = 12345

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print("Receiving audio... Ctrl+C to stop")

audio = bytearray()

try:
    while True:
        data, _ = sock.recvfrom(2048)
        audio.extend(data)
except KeyboardInterrupt:
    pass

with wave.open("output.wav", "wb") as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(8000)
    wf.writeframes(audio)

print("Saved output.wav")

