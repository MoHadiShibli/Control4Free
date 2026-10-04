# Build image for Control4Free payloads.
#
# Holds clang/lld plus a pinned ps4-payload-sdk release, so a build needs nothing
# installed on the host but Docker. Matches the SDK's own CI (clang-18 / lld-18).
FROM ubuntu:24.04

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

# orbis-clang execs $SCRIPT_DIR/clang, and orbis-ld execs $SCRIPT_DIR/ld.lld.
RUN ln -sf /usr/bin/clang-18 /opt/ps4-payload-sdk/bin/clang \
    && ln -sf /usr/bin/clang++-18 /opt/ps4-payload-sdk/bin/clang++ \
    && ln -sf /usr/bin/ld.lld-18 /opt/ps4-payload-sdk/bin/ld.lld

ENV PS4_PAYLOAD_SDK=/opt/ps4-payload-sdk
ENV LLVM_CONFIG=/usr/bin/llvm-config-18
WORKDIR /src
