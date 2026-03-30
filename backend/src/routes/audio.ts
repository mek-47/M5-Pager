import { Elysia, t } from "elysia";
import { unlink } from "node:fs/promises";
import prisma from "../db";

const AUDIO_DIR = "/audio";

const audioRoutes = new Elysia({ prefix: "/api/audio" })

  // GET /api/audio — List all audio records (newest first)
  .get("/", async () => {
    const records = await prisma.audioRecord.findMany({
      orderBy: { createdAt: "desc" },
    });
    return records;
  })

  // GET /api/audio/:id — Get single record metadata
  .get("/:id", async ({ params, set }) => {
    const record = await prisma.audioRecord.findUnique({
      where: { id: params.id },
    });

    if (!record) {
      set.status = 404;
      return { error: "Audio record not found" };
    }

    return record;
  })

  // GET /api/audio/:id/file — Stream the WAV file
  .get("/:id/file", async ({ params, set }) => {
    const record = await prisma.audioRecord.findUnique({
      where: { id: params.id },
    });

    if (!record) {
      set.status = 404;
      return { error: "Audio record not found" };
    }

    const filePath = `${AUDIO_DIR}/${record.filename}`;
    const file = Bun.file(filePath);

    if (!(await file.exists())) {
      set.status = 404;
      return { error: "Audio file not found on disk" };
    }

    set.headers["Content-Type"] = "audio/wav";
    set.headers["Content-Disposition"] = `inline; filename="${record.filename}"`;
    return file;
  })

  // DELETE /api/audio/:id — Delete record + WAV file
  .delete("/:id", async ({ params, set }) => {
    const record = await prisma.audioRecord.findUnique({
      where: { id: params.id },
    });

    if (!record) {
      set.status = 404;
      return { error: "Audio record not found" };
    }

    // Delete the WAV file from disk
    const filePath = `${AUDIO_DIR}/${record.filename}`;
    try {
      await unlink(filePath);
    } catch (err) {
      // File might already be gone — log but don't fail
      console.warn(`[audio] Could not delete file ${filePath}:`, err);
    }

    // Delete DB record
    await prisma.audioRecord.delete({
      where: { id: params.id },
    });

    return { success: true, deleted: params.id };
  });

export default audioRoutes;
