#!/bin/bash
# ============================================================
# 鎌ゲーム(LiDAR) 起動スクリプト
#
# 使い方:
#   ./run.sh          … ビルドしてゲームを起動（キーボードでも遊べる）
#   ./run.sh lidar    … LiDARを先に起動してからゲームを起動（本番用）
#
# ※ M5Stick無しでも起動できます。ゲーム内で 'k' を押すとキーボードモードに切替。
# ============================================================

set -e

# このスクリプトのある場所を基準にする（どこから実行しても動くように）
DIR="$(cd "$(dirname "$0")" && pwd)"
CG="$DIR/cgprog"

cd "$CG"

echo "▶ ビルド中..."
g++ -O3 main.cpp -std=c++11 \
    -framework OpenGL -framework GLUT \
    -I/opt/homebrew/include -L/opt/homebrew/lib -lalut -framework OpenAL \
    -Wno-deprecated -o game

# 引数に lidar が指定されたら、先にLiDARアプリを起動
if [ "$1" = "lidar" ]; then
    echo "▶ LiDARを起動..."
    ( cd "$DIR/LiDAR" && ./lidarapp ) &
    LIDAR_PID=$!
    sleep 2
    echo "  footpoint.txt を確認: $(head -1 "$DIR/LiDAR/footpoint.txt" 2>/dev/null)"
fi

echo "▶ ゲーム起動..."
./game

# ゲーム終了後、LiDARも止める
if [ -n "$LIDAR_PID" ]; then
    kill "$LIDAR_PID" 2>/dev/null || true
fi
