# Stage 1: Build frontend
FROM node:20-alpine AS frontend-build
WORKDIR /app/frontend
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci
COPY frontend/ ./
ARG VITE_MICROSOFT_CLIENT_ID=""
ENV VITE_MICROSOFT_CLIENT_ID=$VITE_MICROSOFT_CLIENT_ID
RUN npm run build

# Stage 2: Build backend
FROM ubuntu:24.04 AS backend-build
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates libssl-dev \
    unixodbc-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app/backend
COPY backend/CMakeLists.txt ./
COPY backend/src/ ./src/
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_AZURE_SQL=ON \
    && cmake --build build --config Release -j$(nproc)

# Stage 3: Runtime
FROM ubuntu:24.04
# Install ODBC driver in two steps to avoid autoremove pulling out dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libssl3 unixodbc gnupg2 curl \
    && curl -fsSL https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor -o /usr/share/keyrings/microsoft.gpg \
    && echo "deb [arch=amd64 signed-by=/usr/share/keyrings/microsoft.gpg] https://packages.microsoft.com/ubuntu/24.04/prod noble main" > /etc/apt/sources.list.d/mssql-release.list \
    && apt-get update \
    && ACCEPT_EULA=Y apt-get install -y msodbcsql18 \
    && rm -rf /var/lib/apt/lists/* \
    && ldconfig

WORKDIR /app
COPY --from=backend-build /app/backend/build/autoplanner ./autoplanner
COPY --from=frontend-build /app/frontend/dist ./static

EXPOSE 8080
ENV PORT=8080

ARG AZURE_SQL_CONNECTION_STRING=""
ENV AZURE_SQL_CONNECTION_STRING=$AZURE_SQL_CONNECTION_STRING

CMD ["sh", "-c", "./autoplanner $PORT ./static"]
