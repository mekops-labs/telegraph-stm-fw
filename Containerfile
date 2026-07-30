# Use the amd64 architecture only.
#
# Note: the ARM cross toolchain and the NuttX kconfig frontends are binaries
# for the host architecture. On an x86_64 host, an arm64 image makes the full
# build run under qemu.
FROM --platform=linux/amd64 docker.io/library/debian:trixie-slim

LABEL maintainer="mek.xgt@gmail.com"

RUN apt-get update \
    && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
    # Toolchain for the host. NuttX builds its own host tools.
    build-essential \
    ccache \
    clang \
    clang-format \
    clang-tidy \
    cmake \
    cppcheck \
    ninja-build \
    # ARM cross toolchain. The STM32F105 has a Cortex-M3 core.
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    gdb-multiarch \
    # These packages are necessary to configure and to build NuttX.
    #
    # Note: the kconfig-frontends package gives the kconfig-conf and the
    # kconfig-tweak programs. The script tools/configure.sh and the menuconfig
    # target need these two programs.
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

# Send all compile steps through ccache.
#
# Note: the package gives symbolic links for gcc and g++ only. The NuttX build
# calls `cc` directly. Thus this step adds links for cc and c++.
RUN ln -sf ../../bin/ccache /usr/lib/ccache/cc \
    && ln -sf ../../bin/ccache /usr/lib/ccache/c++
ENV PATH="/usr/lib/ccache:${PATH}"

# The host user owns the source directory. The user in the container is
# different. Thus git needs this setting.
RUN printf '[safe]\n\tdirectory = *\n' > /etc/gitconfig

WORKDIR /src
CMD ["make", "help"]
