# Runtime image for arbiterAI-server, built from the packaged release tarball
# (scripts/packageServer.sh). This is not the build environment: that is
# docker/Dockerfile.
#
#   docker build -f docker/server.Dockerfile --build-arg VERSION=X.Y.Z \
#       -t arbiterai-server:vX.Y.Z <dir holding arbiterAI-server-vX.Y.Z-linux-x64.tar.gz>
#
# Run it with docker/compose.yaml, which adds the restart policy and
# ARBITERAI_SUPERVISOR=docker.

FROM ubuntu:24.04 AS unpack
ARG VERSION
COPY arbiterAI-server-v${VERSION}-linux-x64.tar.gz /tmp/server.tar.gz
RUN mkdir -p /opt/arbiterAI \
    && tar -xzf /tmp/server.tar.gz --strip-components=1 -C /opt/arbiterAI

FROM ubuntu:24.04
ARG VERSION

# libvulkan1/libgomp1 are the binary's runtime libraries; mesa-vulkan-drivers
# gives AMD/Intel GPUs a Vulkan driver (NVIDIA's comes in through the
# container toolkit); ca-certificates for the HTTPS clone of arbiterAI_config
# and the cloud providers; tini as PID 1.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libvulkan1 libgomp1 mesa-vulkan-drivers ca-certificates tini \
    && rm -rf /var/lib/apt/lists/*

COPY --from=unpack /opt/arbiterAI /opt/arbiterAI

LABEL org.opencontainers.image.title="arbiterAI-server" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.source="https://github.com/caseymcc/arbiterAI"

# The server clones arbiterAI_config into its working directory; keep it on
# the config volume so it survives a new container.
VOLUME ["/config", "/models"]
WORKDIR /config
EXPOSE 8080

# tini is PID 1: it forwards SIGTERM for the graceful drain, reaps children,
# and lets a crash signal the server re-raises (e.g. SIGSEGV) actually kill it,
# which a PID 1 without a handler would ignore.
ENTRYPOINT ["tini", "--", "/opt/arbiterAI/bin/arbiterAI-server"]
CMD ["-c", "/config/server.json"]
