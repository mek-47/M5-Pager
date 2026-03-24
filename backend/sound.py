import socket

PORT = 12345
OUTFILE = "audio.raw"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", PORT))

print("Recording UDP audio...")

with open(OUTFILE, "wb") as f:
    while True:
        data, addr = sock.recvfrom(2048)
        f.write(data)
        print("write", len(data))

