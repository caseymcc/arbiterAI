#!/bin/bash
# Check a runtime image built from docker/server.Dockerfile, the way an
# operator runs it. Used by ci.yml and the release workflows; needs docker,
# git and curl on the host.
#
# Usage: scripts/checkServerImage.sh <image> <version>
#
#   1. --version reports <version>.
#   2. --self-test passes on the Docker example config, with /config mounted
#      read-only (it must write nothing).
#   3. A real start answers /health; docker stop exits 0 with "shutdown complete"
#      well within Docker's 10 s SIGKILL.
#   4. A crash of the server (SIGSEGV sent to it inside the container) gets the
#      container restarted by its restart policy: the crash handler re-raises,
#      the server dies, tini exits with it.
#      (Not "docker kill -s SEGV": that signals tini, which does not forward
#      synchronous signals, and as PID 1 without a handler it ignores them.)
#   5. With a volume at /opt/arbiterAI the entrypoint seeds it from the image,
#      tini stays PID 1, /api/update allows apply with supervisor "docker",
#      and a new container on the same volume keeps it.

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <image> <version>" >&2
    exit 1
fi

IMAGE="$1"
VERSION="${2#v}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT=18080
ID="arbiterai-image-check-$$"
CONFIG_VOLUME="${ID}-config"
MODELS_VOLUME="${ID}-models"
INSTALL_VOLUME="${ID}-install"
WORK="$(mktemp -d)"

cleanup() {
    docker rm -f "${ID}-run" "${ID}-crash" "${ID}-install" >/dev/null 2>&1 || true
    docker volume rm -f "$CONFIG_VOLUME" "$MODELS_VOLUME" "$INSTALL_VOLUME" >/dev/null 2>&1 || true
    rm -rf "$WORK"
}
trap cleanup EXIT

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

waitForHealth() {
    for _ in $(seq 120); do
        if curl -sf "http://127.0.0.1:${PORT}/health" >/dev/null; then
            return 0
        fi
        sleep 0.5
    done
    return 1
}

echo "== 1. --version"
REPORTED="$(docker run --rm "$IMAGE" --version | head -n1)"
[ "$REPORTED" = "arbiterAI-server ${VERSION}" ] || fail "--version reports '${REPORTED}', expected 'arbiterAI-server ${VERSION}'"
echo "$REPORTED"

echo "== 2. --self-test on a read-only /config"
# A fresh install has no arbiterAI_config checkout until its first start, and
# the self-test never fetches one; seed the volume the way a first start would.
git clone --quiet --depth 1 https://github.com/caseymcc/arbiterAI_config.git "${WORK}/arbiterAI_config"
cp "${REPO_ROOT}/packaging/server/examples/server.docker.json" "${WORK}/server.json"
docker volume create "$CONFIG_VOLUME" >/dev/null
docker volume create "$MODELS_VOLUME" >/dev/null
docker run --rm -v "${CONFIG_VOLUME}:/config" -v "${WORK}:/seed:ro" --entrypoint sh "$IMAGE" \
    -c 'cp -r /seed/server.json /seed/arbiterAI_config /config/'

docker run --rm -v "${CONFIG_VOLUME}:/config:ro" -v "${MODELS_VOLUME}:/models:ro" "$IMAGE" \
    --self-test -c /config/server.json || fail "--self-test failed"

echo "== 3. start, then docker stop"
docker run -d --name "${ID}-run" -p "127.0.0.1:${PORT}:8080" \
    -v "${CONFIG_VOLUME}:/config" -v "${MODELS_VOLUME}:/models" "$IMAGE" >/dev/null
waitForHealth || { docker logs "${ID}-run" | tail -50; fail "server did not answer /health"; }

START=$(date +%s)
docker stop -t 30 "${ID}-run" >/dev/null
STOP_SECONDS=$(( $(date +%s) - START ))
EXIT_CODE="$(docker inspect -f '{{.State.ExitCode}}' "${ID}-run")"
docker logs "${ID}-run" 2>&1 | tail -5
[ "$EXIT_CODE" = "0" ] || fail "docker stop: exit code ${EXIT_CODE}, expected 0"
[ "$STOP_SECONDS" -lt 10 ] || fail "docker stop took ${STOP_SECONDS} s"
docker logs "${ID}-run" 2>&1 | grep "shutdown complete" >/dev/null || fail "no 'shutdown complete' in the logs"
echo "stopped cleanly in ${STOP_SECONDS} s"

echo "== 4. a server crash restarts the container"
docker run -d --name "${ID}-crash" --restart on-failure:3 -p "127.0.0.1:${PORT}:8080" \
    -v "${CONFIG_VOLUME}:/config" -v "${MODELS_VOLUME}:/models" "$IMAGE" >/dev/null
waitForHealth || { docker logs "${ID}-crash" | tail -50; fail "server did not answer /health"; }
# The image has no procps; find the server by its (15-character) comm name.
docker exec "${ID}-crash" sh -c \
    'for p in /proc/[0-9]*; do [ "$(cat $p/comm 2>/dev/null)" = "arbiterAI-serve" ] && kill -SEGV "${p#/proc/}"; done; true'

RESTARTS=0
for _ in $(seq 60); do
    RESTARTS="$(docker inspect -f '{{.RestartCount}}' "${ID}-crash")"
    [ "$RESTARTS" -ge 1 ] && break
    sleep 0.5
done
[ "$RESTARTS" -ge 1 ] || { docker logs "${ID}-crash" | tail -20; fail "container was not restarted after SIGSEGV"; }
waitForHealth || fail "server did not come back after the restart"
docker logs "${ID}-crash" 2>&1 | grep "FATAL SIGNAL" >/dev/null || fail "no crash log line before the restart"
echo "restarted ${RESTARTS} time(s) after SIGSEGV"

docker rm -f "${ID}-crash" >/dev/null

echo "== 5. the install volume"
docker volume create "$INSTALL_VOLUME" >/dev/null
runOnInstallVolume() {
    docker run -d --name "${ID}-install" -p "127.0.0.1:${PORT}:8080" -e ARBITERAI_SUPERVISOR=docker \
        -v "${CONFIG_VOLUME}:/config" -v "${MODELS_VOLUME}:/models" -v "${INSTALL_VOLUME}:/opt/arbiterAI" \
        "$IMAGE" >/dev/null
    waitForHealth || { docker logs "${ID}-install" | tail -50; fail "server on the install volume did not answer /health"; }
}

runOnInstallVolume
docker logs "${ID}-install" 2>&1 | grep "Seed: installed v${VERSION}" >/dev/null \
    || { docker logs "${ID}-install" | head -20; fail "the empty install volume was not seeded"; }
[ "$(docker exec "${ID}-install" cat /proc/1/comm)" = "tini" ] || fail "PID 1 is not tini"
UPDATE="$(curl -sf "http://127.0.0.1:${PORT}/api/update")"
echo "$UPDATE" | grep '"can_apply":true' >/dev/null || fail "apply is blocked on the install volume: ${UPDATE}"
echo "$UPDATE" | grep '"supervisor":"docker"' >/dev/null || fail "supervisor is not docker: ${UPDATE}"
docker stop -t 30 "${ID}-install" >/dev/null
docker rm "${ID}-install" >/dev/null

runOnInstallVolume
docker logs "${ID}-install" 2>&1 | grep "Seed: kept v${VERSION}" >/dev/null \
    || { docker logs "${ID}-install" | head -20; fail "a new container did not keep the install volume"; }
echo "seeded, apply allowed under docker, kept by a new container"

echo "image ${IMAGE}: all checks passed"
