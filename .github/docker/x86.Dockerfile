# The compiler/sysroot are Haiku's official r1beta5 hybrid x86 toolchain.
# Use a current host userland for the independently pinned resource/hpkg tools.
FROM ghcr.io/haiku/cross-compiler:x86_gcc2h-r1beta5@sha256:f7b1ff813795c133d9f1c6a71df97aaa0e36a0a7107a02be8b277871d82cee07 AS toolchain
FROM ubuntu:24.04
COPY --from=toolchain /tools /tools
COPY --from=toolchain /system /system
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git curl ca-certificates perl patch \
    pkg-config unzip python3 file libmpc3 libmpfr6 libgmp10 zlib1g libzstd1 \
    && rm -rf /var/lib/apt/lists/*
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs -o /tmp/rustup.sh \
    && sh /tmp/rustup.sh -y --profile minimal --default-toolchain nightly-2026-10-01 --component rust-src \
    && rm /tmp/rustup.sh
ENV PATH="/root/.cargo/bin:${PATH}"
WORKDIR /work
