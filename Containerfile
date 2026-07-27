# Pinned to amd64: the ARM cross toolchain and the NuttX kconfig frontends are
# host-arch binaries, and letting this resolve to arm64 on an x86_64 host
# silently drags the whole build through qemu.
FROM --platform=linux/amd64 docker.io/library/debian:trixie-slim

LABEL maintainer="mek.xgt@gmail.com"

RUN apt-get update \
    && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
    # Host build toolchain (NuttX builds its own host tools during kbuild)
    build-essential \
    ccache \
    cmake \
    ninja-build \
    # ARM cross toolchain — STM32F105 is a Cortex-M3
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    gdb-multiarch \
    # NuttX configure/build prerequisites. kconfig-frontends provides
    # kconfig-conf/kconfig-tweak, which `tools/configure.sh` and `menuconfig`
    # both require.
    kconfig-frontends \
    genromfs \
    gperf \
    flex \
    bison \
    gettext \
    libelf-dev \
    # Utilities
    ca-certificates \
    curl \
    git \
    xxd \
    python-is-python3 \
    python3 \
    python3-venv \
    && rm -rf /var/lib/apt/lists/*

# Route compiles through ccache. The package ships gcc/g++ symlinks; add cc/c++
# because the NuttX kbuild invokes `cc` directly.
RUN ln -sf ../../bin/ccache /usr/lib/ccache/cc \
    && ln -sf ../../bin/ccache /usr/lib/ccache/c++
ENV PATH="/usr/lib/ccache:${PATH}"

# The bind-mounted source is owned by the host user, not by whoever runs here.
RUN printf '[safe]\n\tdirectory = *\n' > /etc/gitconfig

WORKDIR /src
CMD ["make", "help"]
