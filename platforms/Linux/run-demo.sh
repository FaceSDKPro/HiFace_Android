#!/bin/sh
set -eu
umask 077
demo_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$demo_dir/runtime.sh"
resolve_hiface_runtime gui
if [ "${1:-}" = --help ]; then
    exec "$hiface_runtime/bin/hiface_startup" --help
fi
if [ "$#" -gt 1 ]; then
    echo "用法：./run-demo.sh [/dev/video0]" >&2
    exit 2
fi
if [ -z "${DISPLAY:-}" ]; then
    echo "请在 Linux 桌面会话的终端运行本程序，需要 X11 或带 XWayland 的 Wayland。" >&2
    echo "无桌面时可先运行：$demo_dir/self-test.sh" >&2
    exit 1
fi
data_dir=${HIFACE_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/hiface-demo}
export HIFACE_DEMO_ASSETS_DIR="$demo_dir/assets"
exec "$hiface_runtime/bin/hiface_startup" --builtin "$data_dir/hiface-search.db" "${1:-/dev/video0}"
