#!/bin/bash
# 一条命令进镜像模式：停桥 → 跑镜像服务 → Ctrl-C 后自动恢复桥。
# 用法：  ~/Documents/codex/esp32-screen-bringup/tools/mirror/run.sh [fps]
set -e
FPS="${1:-6}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SERVICE="gui/$(id -u)/com.example.dsh-badge"
PLIST="$HOME/Library/LaunchAgents/com.example.dsh-badge.plist"
PY="$HOME/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3"
[ -x "$PY" ] || PY=python3

WAS_RUNNING=0
cleanup() {
  if [ "$WAS_RUNNING" = "1" ]; then
    echo "恢复桥…"
    launchctl bootstrap "$SERVICE" "$PLIST" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if launchctl print "$SERVICE" >/dev/null 2>&1; then
  launchctl bootout "$SERVICE" 2>/dev/null || true
  WAS_RUNNING=1
  sleep 1
fi

echo "镜像模式：牌子上按中键进第 4 页；Ctrl-C 结束并恢复状态牌。"
"$PY" "$ROOT/tools/mirror/mirror_server.py" --fps "$FPS" --enter
