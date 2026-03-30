-- CreateTable
CREATE TABLE "audio_records" (
    "id" TEXT NOT NULL,
    "device_id" TEXT NOT NULL,
    "filename" TEXT NOT NULL,
    "file_path" TEXT NOT NULL,
    "duration_ms" INTEGER,
    "sample_rate" INTEGER NOT NULL DEFAULT 16000,
    "file_size" INTEGER NOT NULL,
    "created_at" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "audio_records_pkey" PRIMARY KEY ("id")
);
