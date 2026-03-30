FROM python:3.11-slim

WORKDIR /app

# Copy requirements first for Docker caching
COPY requirements.txt .

RUN pip install --no-cache-dir -r requirements.txt

COPY . .

# -u flag makes terminal output unbuffered, vital for docker logs
CMD ["python", "-u", "server.py"]