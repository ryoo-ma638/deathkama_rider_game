#!/bin/bash
# ============================================================
# 鎌ゲーム(LiDAR) 起動スクリプト
#
# 使い方:
#   ./run.sh          … PC版を起動（1画面・キーボード操作。誰でもすぐ遊べる）
#   ./run.sh expo     … 展示用2ウィンドウ（床=ゲーム / 壁=UI。M5Stick使用）
#   ./run.sh lidar    … LiDARを先に起動してから展示用2ウィンドウ（本番用）
#
# PC版の操作: 矢印キー=移動 / a=縦振り攻撃 / s=横振り攻撃 / SPACE=画面送り / z=デバッグ用スキップ
# ============================================================

set -e

# このスクリプトのある場所を基準にする（どこから実行しても動くように）
DIR="$(cd "$(dirname "$0")" && pwd)"
CG="$DIR/cgprog"

cd "$CG"

# .env があれば読み込む（M5STICK_PORT など。実ポート名はgit管理外の.envだけに置く）
if [ -f "$DIR/.env" ]; then
    set -a; . "$DIR/.env"; set +a
fi

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

# expo / lidar 指定なら展示用2ウィンドウ、無指定はPC版(1画面)
MODE=""
if [ "$1" = "expo" ] || [ "$1" = "lidar" ]; then
    MODE="expo"
fi

echo "▶ ゲーム起動..."
./game $MODE

# ゲーム終了後、LiDARも止める
if [ -n "$LIDAR_PID" ]; then
    kill "$LIDAR_PID" 2>/dev/null || true
fi
