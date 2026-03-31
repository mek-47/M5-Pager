FROM oven/bun:latest

WORKDIR /app

COPY package.json ./
COPY bun.lock ./

RUN bun install

COPY . .

RUN bunx prisma generate
RUN bunx prisma migrate deploy

EXPOSE 3000

# Use the dev command from your package.json, or fallback to direct execution
CMD ["bun", "run", "dev"]