FROM oven/bun:latest

WORKDIR /app

# Copy dependency files first to cache bun install
COPY package.json ./
COPY bun.lock ./
# If you have bun.lockb, it's good to copy it here too

RUN bun install

COPY . .

# Generate prisma client if the schema file exists
RUN bunx prisma generate || true

EXPOSE 3000

# Use the dev command from your package.json, or fallback to direct execution
CMD ["bun", "run", "dev"]