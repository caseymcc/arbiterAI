#!/bin/bash
#
# Version utility for ArbiterAI
#
# Usage:
#   ./scripts/version.sh get                 # Print current version
#   ./scripts/version.sh bump patch          # Increment patch: 0.1.0 → 0.1.1
#   ./scripts/version.sh bump minor          # Increment minor: 0.1.1 → 0.2.0
#   ./scripts/version.sh bump major          # Increment major: 0.2.0 → 1.0.0
#   ./scripts/version.sh set 2.3.4           # Set explicit version
#
# Operates on the project(arbiterAI VERSION X.Y.Z ...) line in CMakeLists.txt.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_FILE="${ROOT_DIR}/CMakeLists.txt"

get_version()
{
    local version
    version=$(grep -oP '(?<=project\(arbiterAI VERSION )[0-9]+\.[0-9]+\.[0-9]+' "${CMAKE_FILE}")
    if [ -z "${version}" ]; then
        echo "Error: Could not find version in ${CMAKE_FILE}" >&2
        exit 1
    fi
    echo "${version}"
}

set_version()
{
    local new_version="$1"

    # Validate format
    if ! echo "${new_version}" | grep -qP '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        echo "Error: Invalid version format '${new_version}'. Expected X.Y.Z" >&2
        exit 1
    fi

    sed -i "s/project(arbiterAI VERSION [0-9]\+\.[0-9]\+\.[0-9]\+/project(arbiterAI VERSION ${new_version}/" "${CMAKE_FILE}"
    echo "${new_version}"
}

bump_version()
{
    local component="$1"
    local current
    current=$(get_version)

    local major minor patch
    IFS='.' read -r major minor patch <<< "${current}"

    case "${component}" in
        major)
            major=$((major+1))
            minor=0
            patch=0
            ;;
        minor)
            minor=$((minor+1))
            patch=0
            ;;
        patch)
            patch=$((patch+1))
            ;;
        *)
            echo "Error: Unknown component '${component}'. Use: major, minor, patch" >&2
            exit 1
            ;;
    esac

    set_version "${major}.${minor}.${patch}"
}

# --- Main ---

if [ $# -lt 1 ]; then
    echo "Usage: $0 {get|set <version>|bump <major|minor|patch>}" >&2
    exit 1
fi

command="$1"
shift

case "${command}" in
    get)
        get_version
        ;;
    set)
        if [ $# -lt 1 ]; then
            echo "Usage: $0 set <version>" >&2
            exit 1
        fi
        set_version "$1"
        ;;
    bump)
        if [ $# -lt 1 ]; then
            echo "Usage: $0 bump <major|minor|patch>" >&2
            exit 1
        fi
        bump_version "$1"
        ;;
    *)
        echo "Error: Unknown command '${command}'. Use: get, set, bump" >&2
        exit 1
        ;;
esac
