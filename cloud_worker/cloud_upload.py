from google.cloud import storage

def upload():
    client = storage.Client()
    bucket = client.bucket("your-bucket-name")

    blob = bucket.blob("audio.wav")
    blob.upload_from_filename("audio.wav")

    print("Uploaded to cloud!")

if __name__ == "__main__":
    upload()