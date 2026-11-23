#!/bin/bash
# Package arbiterAI-server into the release tarball:
#   <out-dir>/arbiterAI-server-v<version>-linux-x64.tar.gz
# which unpacks to arbiterAI-server-v<version>-linux-x64/{bin,share,README.md,LICENSE}.
# The self-update task depends on this name and layout.
#
# Usage: scripts/packageServer.sh <build-dir> <version> <out-dir>
#   build-dir  CMake build directory holding arbiterAI-server (e.g. build/linux_x64_release)
#   version    X.Y.Z (a leading "v" is accepted); must match the binary's --version
#   out-dir    where the tarball is written
# Prints the tarball path as the last line of output.

set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <build-dir> <version> <out-dir>" >&2
    exit 1
fi

BUILD_DIR="$1"
VERSION="${2#v}"
OUT_DIR="$3"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGING_DIR="${REPO_ROOT}/packaging/server"
PLATFORM="linux-x64"
NAME="arbiterAI-server-v${VERSION}-${PLATFORM}"
TARBALL="${NAME}.tar.gz"

if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: version must be X.Y.Z, got '$2'" >&2
    exit 1
fi

BINARY="${BUILD_DIR}/arbiterAI-server"
if [ ! -x "$BINARY" ]; then
    echo "Error: ${BINARY} not found or not executable" >&2
    exit 1
fi

STAGE_ROOT="$(mktemp -d)"
trap 'rm -rf "$STAGE_ROOT"' EXIT
STAGE="${STAGE_ROOT}/${NAME}"

mkdir -p "${STAGE}/bin" "${STAGE}/share/arbiterAI/schemas"

cp "$BINARY" "${STAGE}/bin/arbiterAI-server"
strip --strip-unneeded "${STAGE}/bin/arbiterAI-server"

# A stale build would ship a binary that reports another version than the
# tarball and install marker say.
BINARY_VERSION="$("${STAGE}/bin/arbiterAI-server" --version | head -n1)"
if [ "$BINARY_VERSION" != "arbiterAI-server ${VERSION}" ]; then
    echo "Error: ${BINARY} reports '${BINARY_VERSION}', expected 'arbiterAI-server ${VERSION}'" >&2
    exit 1
fi

cp "${REPO_ROOT}/schemas/model_config.schema.json" "${STAGE}/share/arbiterAI/schemas/"

cat > "${STAGE}/share/arbiterAI/install.json" <<EOF
{
    "version": "${VERSION}",
    "asset": "${TARBALL}",
    "platform": "${PLATFORM}"
}
EOF

mkdir -p "${STAGE}/share/arbiterAI/systemd" "${STAGE}/share/arbiterAI/examples"
cp "${PACKAGING_DIR}/systemd/arbiterai-server.service" "${STAGE}/share/arbiterAI/systemd/"
cp "${PACKAGING_DIR}/examples/"*.json "${STAGE}/share/arbiterAI/examples/"

cp "${PACKAGING_DIR}/README.md" "${STAGE}/README.md"
cp "${REPO_ROOT}/LICENSE" "${STAGE}/LICENSE"

mkdir -p "$OUT_DIR"
tar --owner=0 --group=0 --numeric-owner -C "$STAGE_ROOT" -czf "${OUT_DIR}/${TARBALL}" "$NAME"

echo "Packaged ${NAME}:"
tar -tzvf "${OUT_DIR}/${TARBALL}"
echo "${OUT_DIR}/${TARBALL}"
