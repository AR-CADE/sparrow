# ==============================================================================
# Sparrow - Headless CI/CD & Test Runner Container
# ==============================================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# 1. Install system toolchain and Wayland / wlroots / OpenGL dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    clang-tools \
    llvm \
    lld \
    meson \
    ninja-build \
    pkg-config \
    git \
    curl \
    wget \
    unzip \
    ca-certificates \
    python3 \
    python3-pip \
    libwayland-dev \
    wayland-protocols \
    libdrm-dev \
    libgbm-dev \
    libinput-dev \
    libxkbcommon-dev \
    libpixman-1-dev \
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    libdisplay-info-dev \
    libseat-dev \
    libliftoff-dev \
    libxcb1-dev \
    libxcb-composite0-dev \
    libxcb-ewmh-dev \
    libxcb-icccm4-dev \
    libxcb-render0-dev \
    libxcb-res0-dev \
    libxcb-xfixes0-dev \
    libx11-xcb-dev \
    liblcms2-dev \
    libudev-dev \
    hwdata \
    glslang-tools \
    wlrctl \
    weston \
    mesa-utils \
    libgl1-mesa-dri \
    && rm -rf /var/lib/apt/lists/*

# 2. Install Flutter SDK
ENV FLUTTER_HOME=/opt/flutter
ENV PATH="${FLUTTER_HOME}/bin:${PATH}"

RUN git clone --depth 1 -b stable https://github.com/flutter/flutter.git ${FLUTTER_HOME} \
    && flutter config --no-analytics \
    && flutter precache --linux

# 3. Create non-root developer user
RUN useradd -m -s /bin/bash sparrow \
    && chown -R sparrow:sparrow ${FLUTTER_HOME}

USER sparrow
WORKDIR /home/sparrow/app

# 4. Environment for headless rendering on virtual CI environments
ENV WLR_BACKENDS=headless
ENV WLR_HEADLESS_OUTPUTS=1
ENV LIBGL_ALWAYS_SOFTWARE=1
ENV SPARROW_NO_REALTIME=1

# Copy source repository
COPY --chown=sparrow:sparrow . .

# 5. Default command: Build release and run automated headless test bot
CMD ["/bin/bash", "-c", "./build.sh release && ./tools/bot/sparrow_bot.sh --headless --duration=10"]
