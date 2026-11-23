#!/bin/sh
# Entrypoint of the arbiterAI-server image, run by tini (PID 1).
#
# The image's own install is /opt/arbiterAI-dist. The server runs from the
# install volume /opt/arbiterAI, so the dashboard's Update button and
# auto-update can replace it and the update survives a new container. --seed
# brings the volume up to the image: an empty volume gets a copy; a newer
# image is swapped in, unless that version failed on this volume before; an
# older image leaves the volume alone (delete the volume to go back).
set -e

DIST=/opt/arbiterAI-dist
INSTALL=/opt/arbiterAI

case "${1:-}" in
    --version|--help|-h)
        exec "$DIST/bin/arbiterAI-server" "$@"
        ;;
esac

"$DIST/bin/arbiterAI-server" --seed "$INSTALL"
exec "$INSTALL/bin/arbiterAI-server" "$@"
