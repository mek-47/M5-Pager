FROM node:20-alpine

WORKDIR /app

COPY package.json ./

# Usually Vite projects use npm or yarn by default, adjust if you use bun here as well
RUN npm install

COPY . .

EXPOSE 5173

# Expose to host so docker can map the port
CMD ["npm", "run", "dev", "--", "--host", "0.0.0.0"]