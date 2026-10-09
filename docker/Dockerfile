# Build image for Control4Free payloads.
#
# Holds clang/lld plus a pinned ps4-payload-sdk release, so a build needs nothing
# installed on the host but Docker. Matches the SDK's own CI (clang-18 / lld-18).
# Docker's official Ubuntu mirror avoids Docker Hub's shared pull quota.
FROM public.ecr.aws/docker/library/ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55

ARG SDK_VERSION=v0.9

# llvm-18 is here for llvm-config-18: the SDK's orbis-ld asks it where ld.lld
# lives, and without it the link fails on an empty path.
RUN apt-get update && apt-get install -y --no-install-recommends \
        bash ca-certificates clang-18 lld-18 llvm-18 curl unzip make python3 xxd \
    && rm -rf /var/lib/apt/lists/*

# The SDK's orbis-* wrappers look for llvm-config-<ver> and exec bin/clang.
RUN ln -sf /usr/bin/clang-18 /usr/bin/clang \
    && ln -sf /usr/bin/clang++-18 /usr/bin/clang++ \
    && ln -sf /usr/bin/ld.lld-18 /usr/bin/ld.lld \
    && ln -sf /usr/bin/llvm-config-18 /usr/bin/llvm-config \
    && test -x /usr/bin/llvm-config-18 \
    && test -x "$(llvm-config-18 --bindir)/ld.lld"

RUN curl -fsSL -o /tmp/sdk.zip \
        "https://github.com/ps4-payload-dev/sdk/releases/download/${SDK_VERSION}/ps4-payload-sdk.zip" \
    && unzip -q /tmp/sdk.zip -d /opt \
    && rm /tmp/sdk.zip \
    && chmod -R +x /opt/ps4-payload-sdk/bin

# The payload's start-up code (crt1.o) comes from a later SDK commit than the
# release: v0.9 has no kernel offsets for firmware 13.52 or 14.00, so a payload
# built with it quits before main() on those consoles. Same exports, built with
# the SDK's own crt/Makefile.
ARG SDK_CRT_COMMIT=573b4a0d0d5fa8695cfeee5f5e2f0447c550035d
RUN mkdir /tmp/sdk-src \
    && curl -fsSL "https://github.com/ps4-payload-dev/sdk/archive/${SDK_CRT_COMMIT}.tar.gz" \
        | tar xz -C /tmp/sdk-src --strip-components=1 \
    && make -C /tmp/sdk-src/crt LLVM_CONFIG=llvm-config-18 DESTDIR=/opt/ps4-payload-sdk install \
    && rm -rf /tmp/sdk-src

# orbis-clang execs $SCRIPT_DIR/clang, and orbis-ld execs $SCRIPT_DIR/ld.lld.
RUN ln -sf /usr/bin/clang-18 /opt/ps4-payload-sdk/bin/clang \
    && ln -sf /usr/bin/clang++-18 /opt/ps4-payload-sdk/bin/clang++ \
    && ln -sf /usr/bin/ld.lld-18 /opt/ps4-payload-sdk/bin/ld.lld

ENV PS4_PAYLOAD_SDK=/opt/ps4-payload-sdk
ENV LLVM_CONFIG=/usr/bin/llvm-config-18
WORKDIR /src
