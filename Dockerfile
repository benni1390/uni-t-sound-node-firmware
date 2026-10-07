# ==============================================================================
# 1. Builder Stage: Install PlatformIO and the project's declared packages
# ==============================================================================
FROM python:3.12-slim AS builder

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_NO_CACHE_DIR=1

# Install system tools needed for building
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp/build-env
RUN pip install platformio==6.2.0

# Resolve the exact platform and packages declared by platformio.ini.
COPY platformio.ini ./
RUN mkdir -p src && \
    printf 'void setup() {}\nvoid loop() {}\n' > src/main.cpp && \
    pio run -e esp32c6 && \
    mkdir -p /opt/platformio-libdeps && \
    cp -a .pio/libdeps/esp32c6 /opt/platformio-libdeps/ && \
    rm -rf src .pio

# Build the tagged source in Docker, without loading the full toolchain image
# into the CI runner's Docker daemon.
FROM builder AS firmware-build

WORKDIR /workspace
COPY platformio.ini ./
COPY include/ ./include/
COPY src/ ./src/
RUN mkdir -p .pio/libdeps/esp32c6 && \
    cp -a /opt/platformio-libdeps/esp32c6/. .pio/libdeps/esp32c6/ && \
    cp include/secrets.example.h include/secrets.h && \
    pio run -e esp32c6

FROM scratch AS firmware-artifacts
COPY --from=firmware-build /workspace/.pio/build/esp32c6/firmware.factory.bin /
COPY --from=firmware-build /workspace/.pio/build/esp32c6/firmware.bin /

# ==============================================================================
# 2. Final Stage: Lean runner image without build bloat
# ==============================================================================
FROM python:3.12-slim AS runner

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    && rm -rf /var/lib/apt/lists/*

# Create a secure, non-privileged user
ARG PIO_UID=1000
RUN useradd --uid "$PIO_UID" --create-home --shell /bin/bash pio-user
USER pio-user
WORKDIR /workspace

# Copy Python packages and the resolved PlatformIO platform/toolchains.
COPY --from=builder /usr/local/lib/python3.12/site-packages /usr/local/lib/python3.12/site-packages
COPY --from=builder /usr/local/bin/pio /usr/local/bin/pio
COPY --from=builder --chown=pio-user:pio-user /root/.platformio /home/pio-user/.platformio
COPY --from=builder --chown=pio-user:pio-user /opt/platformio-libdeps /opt/platformio-libdeps
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh

# Ensure PlatformIO binary path is accessible
ENV PATH="/home/pio-user/.local/bin:${PATH}"

ENTRYPOINT ["sh", "/usr/local/bin/docker-entrypoint.sh"]
