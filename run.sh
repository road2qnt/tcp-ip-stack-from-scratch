#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

if ! make -q build 2>/dev/null; then
    echo "[run.sh] Building..."
    make build
fi

case "${1:-}" in
    --gui|gui|-g)
        exec ./magi_system.out --gui
        ;;
    --help|-h)
        echo "Usage: ./run.sh [--gui]"
        echo "  (no flag)  - run CLI"
        echo "  --gui      - run with SDL2 GUI"
        ;;
    *)
        exec ./magi_system.out
        ;;
esac
