# syntax=docker/dockerfile:1.7

FROM ubuntu:24.04 AS build

# Must match builtin-baseline in vcpkg.json.
ARG VCPKG_COMMIT=9e593bb18ea69cc5095e012465dcd675a822ed0d
# The CMake version vcpkg pins at that commit (scripts/vcpkg-tools.json).
# Ubuntu's packaged CMake is too old for the vcpkg port scripts.
ARG CMAKE_VERSION=4.4.0
ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive \
    VCPKG_ROOT=/opt/vcpkg \
    VCPKG_FORCE_SYSTEM_BINARIES=1 \
    VCPKG_DISABLE_METRICS=1 \
    VCPKG_DEFAULT_BINARY_CACHE=/root/.cache/vcpkg

RUN apt-get update && apt-get install -y --no-install-recommends \
        autoconf \
        autoconf-archive \
        automake \
        bison \
        build-essential \
        ca-certificates \
        curl \
        flex \
        git \
        libtool \
        linux-libc-dev \
        ninja-build \
        pkg-config \
        python3 \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

RUN case "$TARGETARCH" in \
        arm64) cmake_arch=aarch64 ;; \
        amd64) cmake_arch=x86_64 ;; \
        *) echo "unsupported architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac \
    && curl -fsSL "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${cmake_arch}.tar.gz" \
        | tar -xz -C /opt \
    && ln -s "/opt/cmake-${CMAKE_VERSION}-linux-${cmake_arch}/bin/cmake" /usr/local/bin/cmake \
    && ln -s "/opt/cmake-${CMAKE_VERSION}-linux-${cmake_arch}/bin/ctest" /usr/local/bin/ctest \
    && cmake --version

# vcpkg resolves versions from git history, so this has to be a full clone.
RUN git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout --quiet "$VCPKG_COMMIT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src

# Dependencies first so source changes do not trigger a full rebuild.
COPY vcpkg.json ./
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE" \
    && "$VCPKG_ROOT/vcpkg" install --x-install-root=/opt/vcpkg_installed

COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_MANIFEST_INSTALL=OFF \
        -DVCPKG_INSTALLED_DIR=/opt/vcpkg_installed \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        tzdata \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home-dir /app --shell /usr/sbin/nologin app \
    && mkdir -p /app \
    && chown app:app /app

COPY --from=build /src/build/posture_server /usr/local/bin/posture_server

WORKDIR /app
USER app

ENV APP_LISTEN_ADDRESS=0.0.0.0 \
    APP_PORT=8080

EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/api/v1/health || exit 1

ENTRYPOINT ["/usr/local/bin/posture_server"]
