// ビルド手順: cgprog/ に入って下記 g++、または ./run.sh を実行
// cd cgprog
// g++ -O3 main.cpp -std=c++11 -framework OpenGL -framework GLUT -I/opt/homebrew/include -L/opt/homebrew/lib -lalut -framework OpenAL -Wno-deprecated
// ./run.sh    パソコン用   ./run.sh expo    test用(展示版) ./run.sh lidar  LiDARを先に起動してから展示用
#include <iostream>
#include <GLUT/glut.h>  //OpenGL
#include <math.h>  //数学関数
#include <unistd.h>  //シリアル通信用
#include <fcntl.h>  //シリアル通信用
#include <termios.h>  //シリアル通信用

#include <AL/alut.h>  //OpenAL

#include <cstring>    // memset
#include <algorithm>  // std::min

#include <stdlib.h> // for rand(), srand()
#include <time.h>   // for time()

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// M5Stickのポート名は環境変数 M5STICK_PORT から取得（.env に記載・git管理外）。
// 実ポート名は端末固有IDを含むためソースに直書きしない。キーボードのみで遊ぶなら未設定でOK。
#define BAUD_RATE B115200
#define BUFF_SIZE 4096

// --- 関数プロトタイプ宣言 ---
void initGL();
void display();
void reshape(int w, int h);
void keyboard(unsigned char key, int x, int y);
void specialKeyDown(int key, int x, int y);  // 矢印キー押下（キーボード移動）
void specialKeyUp(int key, int x, int y);    // 矢印キー離す
void triggerAttack(int type);  // 攻撃発動(0=縦振り,1=横振り)。M5Stick/キーボード共通・オートエイム込み
void timer(int value);
void initSerial();
int getSerialData();
void analyzeBuffer();
void initAL();
void drawText(const char *string, float x, float y);
void drawFloorScene();
void drawWallScene();
void drawPCScene();      // PC版(1画面統合)の描画振り分け
void drawPCHud();        // PC版: プレイ中のHUD（時間/スコア/HP）
void drawHudImage(GLuint tex, double xLeft, double yCenter, double w, double h); // HUD画像を1枚描く
void drawPCPrompts();    // PC版: タイトル等の操作案内（PRESS SPACE など）
void loadWallAssets();   // 壁用画像の読み込み（展示版=壁窓 / PC版=同一窓 で共用）
GLuint loadTexture(const char* filename);
void spawnEnemy(int type);
void resetGame();
// ▼▼▼ 関数プロトタイプ宣言エリアに追加 ▼▼▼
void drawOutlinedText(const char* str, float x, float y, float scale, bool centered);
void drawNumber(int value, float cx, float cy, float digitW); // 数字を金色画像で中央揃え描画
void drawRank(char grade, float cx, float cy, float size);     // ランク文字を画像で描画

// --- グローバル変数 ---
#define MAXNUM 1000  // LiDARの最大点数

int winW = 1100, winH = 700;  //ウィンドウサイズ（reshapeで更新）
bool pcMode = false;  // true=PC版(1画面統合・キーボード)。引数なし起動でON、"expo"で従来の展示2窓
bool bossHelpActive = false;  // PC版: ボス攻略説明を全画面表示してバトルを一時停止中
bool bossHelpArmed  = true;   // スポットから離れたら再武装（次に踏むと再表示）
int  alertTimer = 0;  // 登場アニメのカウントダウン（0で停止＝スペース待ち）
int  alertType  = 0;  // 1=WAVE2「攻撃封印」 / 2=WAVE3「攻撃解放」＋ボス登場
bool alertActive = false; // WAVE突入アラート表示中（スペースを押すまで継続・PC版のみ）
int    bossShockTimer = 0;                  // ボス近接衝撃波の炎エフェクト残りフレーム
double bossShockX = 0.0, bossShockY = 0.0;  // その発生位置（ボスの座標）
double fr = 50.0;    // ★ 負荷対策で30に下げる
double angle = 0.0;

// シリアル通信関係
int fd;  //シリアルポート
char bufferAll[BUFF_SIZE];  //蓄積バッファデータ
int bufferPoint = 0;  //蓄積バッファデータサイズ
double val[6];  //受信データ

// --- 入力モード管理（ハード無しでも動かすために追加）---
bool useKeyboardMode = false;  // M5Stick未接続時にtrue（キーボードで操作）
bool debugMode = false;        // 'd'キーでトグル: センサー値などを画面に表示
bool kbUp=false, kbDown=false, kbLeft=false, kbRight=false; // 矢印キー移動フラグ
float avgMove = 0.0;  // 移動量の指数移動平均（防御の静止判定をLiDARノイズに強くする）

// --- 音声管理 ---
#define SND_MAX 13 // 音の総数

// 音のID（番号）を定義
enum {
    SND_ATK_H = 0,    // 横振り
    SND_ATK_V,        // 縦振り
    SND_DEFENSE,      // 防御
    SND_E_SHOOT,      // 敵ショット
    SND_BOSS_BEAM2,    // ボスビーム (溜め)
    SND_BOSS_BEAM,   // ★追加: ボスビーム (発射)
    SND_BOSS_EXP,     // ボス爆発
    SND_DMG_PL,       // プレイヤー被弾
    SND_DMG_EN,       // 敵被弾
    SND_CLEAR,        // クリア
    SND_OVER,         // ゲームオーバー
    SND_ITEM_HEAL,    // 回復
    SND_ITEM_BUFF     // 強化
};

ALuint soundData[SND_MAX]; // 配列を大きくする


// ゲームの状態管理
enum GameState {
    STATE_START,
    STATE_RULE,
    STATE_PLAY,
    STATE_RESULT
};
GameState gameState = STATE_START; // スタート画面から開始

// 画面遷移/ランク計算
int sceneTransitionTimer = 0;
int finalRankScore = 0;
char finalRankGrade = 'C';

// --- テクスチャID (画像管理用) ---
GLuint texBg[3]; // 背景 (Stage 1, 2, 3)
GLuint texNum[10];   // リザルト用の金色数字 0〜9（assets/font/num_*.png）
GLuint texRankImg[5]; // リザルト用のランク文字 S/A/B/C/D（assets/font/rank_*.png）
GLuint texStageClear; // ステージクリア演出の金色画像（assets/stage_clear.png）
GLuint texClearFloor;    // 床のゲームクリア映像（assets/clear_floor.png）
GLuint texGameOverFloor; // 床のゲームオーバー映像（assets/gameover_floor.png）
GLuint texNextStage;     // 「NEXT STAGE」の金色画像（床のクリア演出で使う・assets/next_stage.png）
GLuint texHelpSpot;      // ボス戦ヘルプスポットの画像（床の右下に置く・assets/help_spot.png）
GLuint texHelpSpotPlain; // WAVE1用の石のヘルプマーク（assets/help_spot_plain.png）
GLuint texHelpSpotGold;  // WAVE2用の金コインのヘルプマーク（assets/help_spot_gold.png）
GLuint texWave1Help;     // WAVE1の踏むヘルプ全画面（assets/wave1_help.png）
GLuint texWave2Help;     // WAVE2の踏むヘルプ全画面（assets/wave2_help.png）
GLuint texAlertSealed;   // 突入アラート「攻撃封印！」（assets/attack_sealed.png・無ければ英字）
GLuint texAlertUnsealed; // 突入アラート「攻撃解放！」（assets/attack_unsealed.png・無ければ英字）
bool   hasHelpPlain = false, hasHelpGold = false, hasAlertSealed = false, hasAlertUnsealed = false; // 画像が在るか
double alertSealedAR = 3.0, alertUnsealedAR = 3.25;  // バナー画像の縦横比（読込時に実値へ更新）
GLuint texWarning; bool hasWarning = false; double arWarning = 3.5;  // ボス登場のWARNING画像（無ければ英字）
GLuint texPressStart; bool hasPressStart = false; double arPressStart = 5.31; // 開始案内バー（無ければ英字）
GLuint texTitleFloor; bool hasTitleFloor = false; // 展示版の床タイトル（鎌の魔法陣・STATE_STARTの床のみ）
GLuint texReturnBattle; bool hasReturnBattle = false; double arReturnBattle = 7.61; // ヘルプ「バトルにもどる」案内（無ければ英字）
GLuint texSkipPrompt; bool hasSkipPrompt = false; double arSkipPrompt = 5.61; // ステージクリアの「スペースでスキップ」案内
GLuint texWave2Notice, texNokori, texTai;  // WAVE2の日本語HUD（案内/のこり/たい）
bool   hasWave2Notice = false, hasNokori = false, hasTai = false;
double arNotice = 10.29, arNokori = 2.57, arTai = 1.33;  // それらの縦横比（読込時に実値へ更新）
GLuint texNumFloor[10];  // 床コンテキスト用の金色数字（カウントダウン用。texNumは壁コンテキストなので床では使えないため別途読み込む）
GLuint texWallWave1; // 1壁.jpg
GLuint texWallWave2; // 2壁.jpg
GLuint texWallWave3; // 3壁.jpg

// ▼▼▼ エフェクト用テクスチャID (追加) ▼▼▼
GLuint texEffAtkH;    // 横振り (se_attack_h.jpg)
GLuint texEffAtkV;    // 縦振り (se_attack_v.jpg)
GLuint texEffDef;     // 防御 (防御.jpg)
GLuint texBullEnemy;  // 敵通常弾 (se_enemy_shoot.jpg)
GLuint texBullRef;    // 反射弾 (反射弾.jpg)
GLuint texBossExpCharge; // ★追加: 範囲攻撃 溜め用 (boss_exp.jpg)
GLuint texBossExpFire;   // ★追加: 範囲攻撃 発動用 (boss_exp2.jpg)
GLuint texBossDefense;   // ★追加: 防御用 (boss_df.jpg)
GLuint texBossCharge; // ★追加: ボス溜め中画像
GLuint texBossDamaged; // ★追加: ボスダメージ画像
GLuint texBossBeamC;  // ボスビーム溜め (se_boss_beam1.png)
GLuint texBossBeamF;  // ボスビーム発射 (se_boss_beam2.jpg)
GLuint texBossExp;    // ボス爆発 (se_boss_exp.jpg)
GLuint texBossExp2;    // ★追加: 投石画像2
GLuint texBossExp3;    // ★追加: 投石画像3
GLuint texBossShock;   // ボス近接衝撃波の炎（se_boss_shock.png）
GLuint texBoss;           // ボス (Stage 3)
GLuint texMob1, texMob2;  // モブ (Stage 1)
GLuint texMob3, texMob4;  // モブ (Stage 2)
GLuint texTitle;     // スタート画面用
GLuint texRule;      // ルール画面用（旧）
GLuint texRule1, texRule2; // ルール説明 2ページ（①遊び方 ②ボス攻撃パターン）
int rulePage = 0;          // ルール画面の現在ページ（0=遊び方, 1=ボス攻撃）
GLuint texClear;     // クリア画面用
GLuint texGameOver;  // ゲームオーバー画面用


// LiDAR関係
int footNum = 0; // 検出した点の数
float footPos[MAXNUM][2]; // 各点の座標 (x, y)
int sweepEffectTimer = 0;
int swingDownEffectTimer = 0;

// --- 敵クラス ---
struct Enemy {
    bool active;
    int type; // 0:雑魚, 1:ボス
    float x, y;
    int hp;
    int maxHp;
    int locationIndex; 
    int attackTimer;
    float targetX, targetY;
    int attackPattern;
    
    int hitTimer;
    GLuint textureID; // この敵が使うテクスチャのID

    // ▼▼▼ ボスの溜め攻撃用に追加 ▼▼▼
    bool isCharging;   // 溜め中フラグ
    int chargeTimer;   // 溜め時間計測
    int closeRangeTimer; // 【新機能】プレイヤーが近接ゾーンに居続けた時間（近づきすぎペナルティ用）
};

#define MAX_ENEMIES 10
Enemy enemies[MAX_ENEMIES]; // 敵の配列

bool isDefending = false;
int defenseEffectTimer = 0;

int score = 0;
int currentWave = 1;
int enemiesKilledInWave2 = 0; // WAVE 2 撃破数
int enemiesTargetWave2 = 4;   // WAVE 2 目標数
int enemiesKilledThisWave = 0;
int enemiesInWave = 4;//WAVE 2の目標数
int enemyRespawnTimer = 0;

// --- 新しいグローバル変数 ---
int playerHP = 100;
int enemyHP = 50;
int enemyMaxHP = 50;

#define MAX_FOOTPRINTS 50
float footprintPos[MAX_FOOTPRINTS][2];
int footprintIndex = 0;
int footprintCount = 0;

// 弾の設計図
struct Projectile {
    bool active;       // 弾が有効か
    bool isEnemyBullet; // 敵の弾か（falseなら反射弾）
    float x, y;        // 現在位置
    float vx, vy;      // 速度ベクトル
    
    // ★ 追加: 弾の種類と向き
    int type; // 0:通常弾, 1:斬撃ビーム
    float angle; // 描画回転用（ビームの向き）

    GLuint textureID;//追加: 弾のテクスチャIDを個別に保持 
};

#define MAX_BULLETS 50 // 同時に存在できる弾の最大数
Projectile bullets[MAX_BULLETS]; // 弾を格納する配列

// int enemyAttackTimer = 0; // ★ 個別管理になったため削除（不要）

// 攻撃の「振り抜き・戻り」判定用フラグ
bool canAttackSide = true; // 横振り可能か
bool canAttackVert = true; // 縦振り可能か

bool bossIsDefending = false;  // ボスが防御中か
int bossDefenseTimer = 0;   // ボスの防御/クールダウンタイマー

// LiDAR静止判定用
float prevFootPos[2] = {0.0, 0.0}; // 1フレーム前の座標
int lidarStillTimer = 0;           // 静止している時間

bool canAttack = true; // 攻撃可能フラグ

// ゲームオーバー管理
bool isGameOver = false;

int playerDamageTimer = 0; // 画面揺れと赤フラッシュ用

float playerAngle = 90.0; // プレイヤーの現在の向き（度）。90度が画面上（Y+）方向

// --- アイテム関連 ---
struct Item {
    bool active;
    int type; // 0:回復, 1:攻撃UP
    float x, y;
};
#define MAX_ITEMS 5
Item items[MAX_ITEMS];

int itemSpawnTimer = 0;   // 20秒ごとの生成用タイマー
int attackBuffTimer = 0;  // 攻撃力UPの効果時間
int healEffectTimer = 0;  // 回復時の緑色エフェクト用
int reflectFlashTimer = 0; // 反射成功時の発光演出用

GLuint texItemHeal; // 回復薬の画像
GLuint texItemBuff; // 強化アイテムの画像


int windowID_floor = 0;
int windowID_wall = 0;
GLuint texWallBg;   // ★追加: 壁側ディスプレイの背景画像ID


float spawnLocations[8][2] = {
    {0.0, 600.0}, {-450.0, 600.0}, {450.0, 600.0}, // 北、北西、北東
    {0.0, 100.0}, {-450.0, 100.0}, {450.0, 100.0}, // 南、南西、南東
    {-500.0, 350.0}, {500.0, 350.0}                // 西、東
};

int gameTimer = 30 * 60; // WAVE 1 の制限時間 (30秒 * 60fps)

// ▼▼▼ グローバル変数の追加・修正 ▼▼▼

// WAVEクリア後の待機時間用タイマー (5秒 * 60fps = 300)
int waveClearTimer = 0;

// 演出用の共通フレームカウンタ（スポットの明滅などアニメに使う・毎フレーム+1）
unsigned int gFrame = 0;

// 紙吹雪（パーティクル）の構造体
struct Particle {
    bool active;
    float x, y;
    float vx, vy;
    float r, g, b; // 色
    float size;
};

#define MAX_CONFETTI 100
Particle confetti[MAX_CONFETTI];

// 紙吹雪を初期化する関数（後で呼び出します）
void initConfetti() {
    for (int i = 0; i < MAX_CONFETTI; i++) {
        confetti[i].active = false;
    }
}

// 紙吹雪を発生させる関数
void spawnConfetti() {
    for (int i = 0; i < MAX_CONFETTI; i++) {
        if (!confetti[i].active) {
            confetti[i].active = true;
            confetti[i].x = (rand() % 1100) - 550.0; // 全幅にランダム
            confetti[i].y = 700.0; // 上から降ってくる
            confetti[i].vx = ((rand() % 100) - 50) / 10.0; // 横揺れ
            confetti[i].vy = -((rand() % 50) + 20) / 10.0; // 落下速度
            // カラフルな色設定
            int colorType = rand() % 5;
            if (colorType == 0) { confetti[i].r=1.0; confetti[i].g=0.0; confetti[i].b=0.0; } // 赤
            else if (colorType == 1) { confetti[i].r=0.0; confetti[i].g=1.0; confetti[i].b=0.0; } // 緑
            else if (colorType == 2) { confetti[i].r=0.0; confetti[i].g=0.0; confetti[i].b=1.0; } // 青
            else if (colorType == 3) { confetti[i].r=1.0; confetti[i].g=1.0; confetti[i].b=0.0; } // 黄
            else { confetti[i].r=1.0; confetti[i].g=0.0; confetti[i].b=1.0; } // 紫
            
            confetti[i].size = (rand() % 10) + 5.0;
            // 一度に全部出さず、少しずつ出すためにループを抜ける（調整可）
            if (rand() % 2 == 0) break; 
        }
    }
}

void drawText(const char *string, float x, float y)
{
    // 影（黒）を少しずらして描く → 背景に埋もれず読みやすくなる
    glColor3d(0.0, 0.0, 0.0);
    glRasterPos2f(x + 2.0, y - 2.0);
    for (const char *s = string; *s; s++) {
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *s);
    }
    // 本体（白）
    glColor3d(1.0, 1.0, 1.0);
    glRasterPos2f(x, y);
    while (*string) {
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, *string++);
    }
}



// ▼▼▼ テクスチャ読み込み関数 ▼▼▼
GLuint loadTexture(const char* filename) {
    GLuint textureID;
    glGenTextures(1, &textureID);

    // 画像は assets/ にまとめてあるので、パスを前置する
    char path[256];
    snprintf(path, sizeof(path), "assets/%s", filename);

    int width, height, nrComponents;
    unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format;
        if (nrComponents == 1) format = GL_RED;
        else if (nrComponents == 3) format = GL_RGB;
        else if (nrComponents == 4) format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        // 行のバイト数が4の倍数でない画像(例: 幅882のRGB=2646B)でも崩れないよう、
        // 行アライメントを1にする。これが無いと行ごとに横滑り＋色崩れ（グレーの走査線）になる。
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    } else {
        std::cout << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }

    return textureID;
}

// 画像ファイルが assets/ に在るか（未生成のバナーは英字表示にフォールバックするため）
bool assetExists(const char* filename) {
    char path[256];
    snprintf(path, sizeof(path), "assets/%s", filename);
    int w, h, c;
    return stbi_info(path, &w, &h, &c) != 0;
}

// 画像の縦横比(w/h)を返す。無ければfallback（バナーを歪ませず表示するため）
double assetAspect(const char* filename, double fallback) {
    char path[256];
    snprintf(path, sizeof(path), "assets/%s", filename);
    int w, h, c;
    if (stbi_info(path, &w, &h, &c) && h > 0) return (double)w / (double)h;
    return fallback;
}

// 展示版(LiDAR)は体の動作で説明したいので _expo 版があれば優先。PC版・ファイルが無い時は基本画像。
const char* helpVariant(const char* baseName, const char* expoName) {
    if (!pcMode && assetExists(expoName)) return expoName;
    return baseName;
}


// ▼▼▼ 敵スポーン関数 (完全版) ▼▼▼
void spawnEnemy(int type) {
    int slot = -1;
    for(int i=0; i<MAX_ENEMIES; i++) {
        if(!enemies[i].active) { slot = i; break; }
    }
    if (slot == -1) return; 

    int locIdx = -1;
    int attempts = 0;
    while(attempts < 20) {
        int candidate = rand() % 8;
        bool occupied = false;
        for(int i=0; i<MAX_ENEMIES; i++) {
            if(enemies[i].active && enemies[i].locationIndex == candidate) {
                occupied = true; break;
            }
        }
        if (!occupied) { locIdx = candidate; break; }
        attempts++;
    }
    if (locIdx == -1) locIdx = rand() % 8; 

    enemies[slot].active = true;
    enemies[slot].type = type;
    enemies[slot].locationIndex = locIdx;
    
    // ★ ここが重要: 攻撃タイマーの初期化
    enemies[slot].attackTimer = (type == 1) ? 60 : (rand() % 240);
    enemies[slot].attackPattern = 0; 


    enemies[slot].hitTimer = 0;
    enemies[slot].closeRangeTimer = 0;
    enemies[slot].isCharging = false;  // 前のボスの溜め状態を引き継がないよう初期化
    enemies[slot].chargeTimer = 0;

    if (type == 1) { // ボス (WAVE 3)
        enemies[slot].x = spawnLocations[0][0]; 
        enemies[slot].y = spawnLocations[0][1];
        enemies[slot].targetX = 0.0; 
        enemies[slot].targetY = 400.0;
        enemies[slot].maxHp = 2000; 
        enemies[slot].hp = 2000;
        
        // ★ ボスのテクスチャを設定
        enemies[slot].textureID = texBoss;

    } else { // 雑魚
        enemies[slot].x = spawnLocations[locIdx][0];
        enemies[slot].y = spawnLocations[locIdx][1];
        enemies[slot].maxHp = (currentWave == 1) ? 30 : (pcMode ? 50 : 80); // PC版WAVE2は反射1発(50ダメージ)で倒せるよう50
        enemies[slot].hp = enemies[slot].maxHp;
        
        // ★ WAVEに応じてランダムにテクスチャを設定
        if (currentWave == 1) {
            // mob1 か mob2 をランダムに
            enemies[slot].textureID = (rand() % 2 == 0) ? texMob1 : texMob2;
        } else {
            // WAVE 2 (以降) は mob3 か mob4 をランダムに
            enemies[slot].textureID = (rand() % 2 == 0) ? texMob3 : texMob4;
        }
    }
}

void resetGame() {
    playerHP = 100;
    score = 0;
    currentWave = 1;
    alertTimer = 0; alertType = 0; alertActive = false; // WAVE突入アラートの状態も初期化
    bossHelpActive = false; bossHelpArmed = true;  // 踏むヘルプの状態も初期化
    enemiesKilledInWave2 = 0;
    enemiesKilledThisWave = 0;
    enemyRespawnTimer = 0;
    gameTimer = 30 * 60; // WAVE 1 時間 (30秒)
    
    // プレイヤー位置・向きリセット
    playerAngle = 90.0;
    footNum = 1; footPos[0][0] = 0.0; footPos[0][1] = 100.0;

    // 敵・弾の全消去
    for(int i=0; i<MAX_ENEMIES; i++) enemies[i].active = false;
    for(int i=0; i<MAX_BULLETS; i++) bullets[i].active = false;
    
    // フラグリセット
    isDefending = false;
    bossIsDefending = false;
    
    // ★ 重要: これをfalseにしないと、ゲームオーバー画面から復帰できません
    isGameOver = false; 
    
    attackBuffTimer = 0; // 攻撃UP解除
    healEffectTimer = 0; // 回復演出解除
    for(int i=0; i<MAX_ITEMS; i++) items[i].active = false; // 落ちているアイテムも消す

    // WAVE 1 開始 (最初に敵を4体出す)
    spawnEnemy(0); spawnEnemy(0); spawnEnemy(0); spawnEnemy(0);
}


// シリアルポート初期化
// 攻撃発動（M5Stick・キーボード共通）。type: 0=縦振り, 1=横振り
// オートエイム: 攻撃した瞬間、200以内で最も近い敵の方を自動で向く
void triggerAttack(int type) {
    if (gameState != STATE_PLAY || !canAttack) return;
    if (bossHelpActive) return;   // PC版: ボス攻略ヘルプ表示中は攻撃を止める
    if (alertActive) return;      // PC版: WAVE突入アラート表示中は攻撃を止める
    if (pcMode && currentWave == 2) return;  // PC版WAVE2: 縦横攻撃を封印し「反射のみ」で倒すステージにする

    if (type == 0) {
        swingDownEffectTimer = 10;
        alSourcePlay(soundData[SND_ATK_V]);
    } else {
        sweepEffectTimer = 15;
        alSourcePlay(soundData[SND_ATK_H]);
    }
    canAttack = false;

    // オートエイム
    if (footNum > 0) {
        int targetIndex = -1;
        double minDist = 200.0;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].active) {
                double ex = enemies[i].x - footPos[0][0];
                double ey = enemies[i].y - footPos[0][1];
                double dist = sqrt(ex*ex + ey*ey);
                if (dist < minDist) { minDist = dist; targetIndex = i; }
            }
        }
        if (targetIndex != -1) {
            double ex = enemies[targetIndex].x - footPos[0][0];
            double ey = enemies[targetIndex].y - footPos[0][1];
            playerAngle = atan2(ey, ex) * 180.0 / M_PI;
        }
    }
}

void initSerial()
{
    struct termios tio;

    // ポート名は環境変数 M5STICK_PORT（.env に記載・git管理外）から取得。
    // 未設定 or 開けない時は、終了せずキーボード操作モードで起動を続行する。
    const char* devName = getenv("M5STICK_PORT");
    if (!devName || devName[0] == '\0') {
        printf("M5STICK_PORT is not set (see .env.example).  -> Keyboard mode.\n");
        useKeyboardMode = true;
        return;
    }
    fd = open(devName, O_RDWR | O_NONBLOCK );
    if(fd<0) {
        printf("M5Stick not found (%s).  -> Keyboard mode.\n", devName);
        useKeyboardMode = true;
        return;
    }
    printf("init serial port\n");
    
    memset(&tio, 0, sizeof(tio));
    tio.c_cflag = CS8 | CLOCAL | CREAD;
    tio.c_cc[VTIME] = 100;
    cfsetispeed(&tio, BAUD_RATE);
    cfsetospeed(&tio, BAUD_RATE);
    tcsetattr(fd, TCSANOW, &tio);
}


int main(int argc, char *argv[])
{
    

    srand(time(NULL)); // 乱数のシードを初期化

    // 起動引数: なし=PC版(1画面・キーボード操作) / "expo"=展示用2ウィンドウ(床+壁)
    pcMode = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "expo") == 0) pcMode = false;
    }

    alutInit(&argc, argv);
    initAL();
    if (!pcMode) initSerial();   // PC版はキーボード専用（M5Stickを探しに行かない）
    glutInit(&argc, argv);

    // ==========================================
    // PC版: 1ウィンドウに統合（床ゲーム全面＋壁UIを右上の小窓に）
    // ==========================================
    if (pcMode) {
        fd = -1;  // シリアル未使用の印（誤ってstdinを読まないように）

        glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
        glutInitWindowSize(1100, 700);
        windowID_floor = glutCreateWindow("Death Kama Rider");
        windowID_wall  = windowID_floor;   // タイマー等の窓IDは同一窓を指す

        glutDisplayFunc(display);
        glutReshapeFunc(reshape);
        glutKeyboardFunc(keyboard);
        glutSpecialFunc(specialKeyDown);
        glutSpecialUpFunc(specialKeyUp);
        glutTimerFunc(1000/fr, timer, 0);

        initGL();          // 床用の画像
        loadWallAssets();  // 壁用の画像も同じウィンドウに読み込む

        // 最初からキーボードで遊べる状態にする（'k'でM5切替も可）
        useKeyboardMode = true;
        footNum = 1; footPos[0][0] = 0.0; footPos[0][1] = 100.0;

        glutTimerFunc(1000/fr, timer, 0); // 展示版と同じタイマー2系統（ゲーム速度を合わせるため）
        glutMainLoop();
        alutExit();
        return 0;
    }

    // ==========================================
    // 1. 床側 (メイン) ウィンドウ作成
    // ==========================================
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(1100, 700);
    windowID_floor = glutCreateWindow("Floor Display (Main)");

    // 床側の機能設定
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);       // キーボード操作は床側で受付
    glutSpecialFunc(specialKeyDown);  // 矢印キー押下（キーボードモードの移動）
    glutSpecialUpFunc(specialKeyUp);  // 矢印キー離す
    glutTimerFunc(1000/fr, timer, 0); // タイマーも床側で管理
    
    initGL(); // 床用の画像を読み込み (さきほどの関数)


// [修正] 374行目以降の main関数内

    // 2. 壁側(サブ) ウィンドウ作成
    glutInitWindowPosition(1150, 0);
    glutInitWindowSize(1100, 700);
    windowID_wall = glutCreateWindow("Wall Display");

    // 壁側の機能設定
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);

    // ★壁側の初期設定＋壁用画像の読み込み（PC版と共用の関数にまとめた）
    loadWallAssets();

    // メインループ開始の直前に入れる
glutTimerFunc(1000/fr, timer, 0); // ★これがないとタイマーが始動しません

glutMainLoop();
    
    alutExit();
    return 0;
    
    
    
    
    // 終了処理（追加）
    alutExit();
    return 0;
}

// 壁側の初期設定＋壁用画像の読み込み（展示版=壁ウィンドウ / PC版=統合ウィンドウ で共用）
void loadWallAssets()
{
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    // ▼▼▼ 壁用画像の読み込み ▼▼▼
    texWallWave1 = loadTexture("1壁.png");
    texWallWave2 = loadTexture("2壁.png");
    texWallWave3 = loadTexture("3壁.png");

    // リザルト用の金色数字・ランク画像
    for (int i = 0; i < 10; i++) {
        char p[64];
        snprintf(p, sizeof(p), "font/num_%d.png", i);
        texNum[i] = loadTexture(p);
    }
    const char* rk = "SABCD";
    for (int i = 0; i < 5; i++) {
        char p[64];
        snprintf(p, sizeof(p), "font/rank_%c.png", rk[i]);
        texRankImg[i] = loadTexture(p);
    }

    // スタート・ルール・結果画面（壁に出すためこのコンテキストでも読み込む）
    texTitle = loadTexture(helpVariant("title.png", "title_expo.png"));  // PC=スペース版/展示=鎌を振る版
    // texRule（旧ルール画面 rule.jpg）は未使用のため読み込み廃止（rule1/rule2 に統合済み）
    texRule1 = loadTexture(helpVariant("rule1.png", "rule1_expo.png")); // 遊び方（PC=キー版/展示=動作名版）
    texRule2 = loadTexture("rule2.png"); // ルール②ボス攻撃パターン（ボス戦のヘルプにも使う）
    texClear = loadTexture(helpVariant("clear.png", "clear_expo.png"));        // PC=スペース版/展示=鎌を振る版
    texGameOver = loadTexture(helpVariant("gameover.png", "gameover_expo.png")); // 同上
}

void display()
{
    if (pcMode) {           // PC版: 1画面に統合して描く
        drawPCScene();
        glutSwapBuffers();
        return;
    }

    int currentWindow = glutGetWindow();

    if (currentWindow == windowID_floor) {
        // ★ここに必要なロジック全体を定義/呼び出しが必要です
        drawFloorScene();
    } else if (currentWindow == windowID_wall) {
        drawWallScene();
    }

    glutSwapBuffers();
}

// PC版(1画面統合)の描画:
//   タイトル/ルール/リザルト = 壁の画面を全面に＋操作案内（PRESS SPACE など）
//   プレイ中               = 床のゲームを全面に＋PC用HUD（時間/スコア/HP）を重ねる
void drawPCScene()
{
    // ウィンドウをどう伸ばしても絵が歪まないよう、11:7の比率を保って中央に表示（余りは黒帯）
    int vw = winW, vh = winH, vx = 0, vy = 0;
    if (winW * 700 > winH * 1100) { vw = winH * 1100 / 700;  vx = (winW - vw) / 2; }
    else                          { vh = winW * 700  / 1100; vy = (winH - vh) / 2; }
    glViewport(vx, vy, vw, vh);

    if (gameState != STATE_PLAY) {
        drawWallScene();     // タイトル・ルール・リザルトは壁の画面をそのまま全面に
        drawPCPrompts();     // 操作案内を重ねる
        return;
    }

    drawFloorScene();        // ゲーム本編を全面に
    drawPCHud();             // PC用HUDを重ねる

    // ボス戦ヘルプ: スポットに乗ると全画面で攻略説明(rule2)を表示（この間バトルは停止）
    if (bossHelpActive) {
        glLoadIdentity();
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // 表示する画像はWAVEで切替（1=遊び方 / 2=反射の説明 / 3=ボス攻撃パターン）
        GLuint helpTex = (currentWave == 1) ? texWave1Help
                       : (currentWave == 2) ? texWave2Help
                       : texRule2;
        glBindTexture(GL_TEXTURE_2D, helpTex);
        glColor4d(1.0, 1.0, 1.0, 1.0);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-550.0, 700.0);
            glTexCoord2f(0.0, 1.0); glVertex2d(-550.0, 0.0);
            glTexCoord2f(1.0, 1.0); glVertex2d(550.0, 0.0);
            glTexCoord2f(1.0, 0.0); glVertex2d(550.0, 700.0);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        // 下部に「スペースで戻る」案内（点滅・半透明バーの上）
        glColor4d(0.0, 0.0, 0.0, 0.55);
        glBegin(GL_QUADS);
            glVertex2d(-550.0, 70.0); glVertex2d(550.0, 70.0);
            glVertex2d(550.0, 0.0);   glVertex2d(-550.0, 0.0);
        glEnd();
        if ((gFrame / 25) % 2 == 0) {
            if (hasReturnBattle) { double ph = 44.0; drawHudImage(texReturnBattle, -ph * arReturnBattle / 2.0, 33.0, ph * arReturnBattle, ph); }
            else drawOutlinedText("PRESS SPACE TO RETURN TO BATTLE", 0.0, 22.0, 0.26, true);
        }
    }

    // ▼ WAVE突入アラート（スペースを押すまで表示・押すとWAVE開始）: 1=攻撃封印 2=攻撃解放＋ボス登場
    if (alertActive) {
        glLoadIdentity();
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        double full = (alertType == 2) ? 3.0 * fr : 2.5 * fr;  // 総フレーム数
        double p    = 1.0 - (double)alertTimer / full;         // 進行度 0→1

        // 画面暗転（立ち上がりだけ・以降はキープ）
        double dark = 0.62;
        if (p < 0.15) dark *= p / 0.15;
        glColor4d(0.0, 0.0, 0.0, dark);
        glBegin(GL_QUADS);
            glVertex2d(-550.0, 700.0); glVertex2d(550.0, 700.0);
            glVertex2d(550.0, 0.0);    glVertex2d(-550.0, 0.0);
        glEnd();

        // 開幕フラッシュ（最初の一瞬。封印=赤 / 解放=金）
        if (p < 0.12) {
            double fa = (0.12 - p) / 0.12 * 0.85;
            if (alertType == 2) glColor4d(1.0, 0.9, 0.5, fa);
            else                glColor4d(0.85, 0.1, 0.15, fa);
            glBegin(GL_QUADS);
                glVertex2d(-550.0, 700.0); glVertex2d(550.0, 700.0);
                glVertex2d(550.0, 0.0);    glVertex2d(-550.0, 0.0);
            glEnd();
        }

        // ボス登場（type2）: 上下の黒帯＋ボスがズームイン＋振動＋WARNING
        if (alertType == 2) {
            double bp = (p < 0.2) ? p / 0.2 : 1.0;   // 立ち上がりだけ・以降キープ
            if (bp < 0.0) bp = 0.0; if (bp > 1.0) bp = 1.0;
            double bh = 90.0 * bp;
            glDisable(GL_TEXTURE_2D);
            glColor4d(0.0, 0.0, 0.0, 0.9);
            glBegin(GL_QUADS);
                glVertex2d(-550.0, 700.0);     glVertex2d(550.0, 700.0);
                glVertex2d(550.0, 700.0 - bh); glVertex2d(-550.0, 700.0 - bh);
                glVertex2d(-550.0, bh);         glVertex2d(550.0, bh);
                glVertex2d(550.0, 0.0);         glVertex2d(-550.0, 0.0);
            glEnd();

            double zt = (p - 0.15) / 0.45;
            if (zt < 0.0) zt = 0.0; if (zt > 1.0) zt = 1.0;
            double sc = 0.25 + 0.75 * zt;                        // 0.25→1.0
            double shake = (p < 0.65) ? sin((double)gFrame * 0.9) * 10.0 * (1.0 - zt) : 0.0;
            double bwh = 320.0 * sc;
            double cx = shake, cy = 440.0;
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texBoss);
            glColor4d(1.0, 1.0, 1.0, (zt < 0.3 ? zt / 0.3 : 1.0));
            glBegin(GL_QUADS);
                glTexCoord2f(0.0, 0.0); glVertex2d(cx - bwh, cy + bwh);
                glTexCoord2f(0.0, 1.0); glVertex2d(cx - bwh, cy - bwh);
                glTexCoord2f(1.0, 1.0); glVertex2d(cx + bwh, cy - bwh);
                glTexCoord2f(1.0, 0.0); glVertex2d(cx + bwh, cy + bwh);
            glEnd();
            glDisable(GL_TEXTURE_2D);

            if (p > 0.15 && (gFrame / 6) % 2 == 0) {
                if (hasWarning) {
                    double hh = 42.0, hw = hh * arWarning;   // 中央上・点滅（画像）
                    glEnable(GL_TEXTURE_2D);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glBindTexture(GL_TEXTURE_2D, texWarning);
                    glColor4d(1.0, 1.0, 1.0, 1.0);
                    glBegin(GL_QUADS);
                        glTexCoord2f(0.0, 0.0); glVertex2d(-hw, 655.0 + hh);
                        glTexCoord2f(0.0, 1.0); glVertex2d(-hw, 655.0 - hh);
                        glTexCoord2f(1.0, 1.0); glVertex2d( hw, 655.0 - hh);
                        glTexCoord2f(1.0, 0.0); glVertex2d( hw, 655.0 + hh);
                    glEnd();
                    glDisable(GL_TEXTURE_2D);
                } else {
                    drawOutlinedText("WARNING", 0.0, 650.0, 0.5, true);   // 画像が無い間は英字
                }
            }
        }

        // 中央バナー（攻撃封印！/攻撃解放！）: スラムイン→キープ（消えない）
        double bt = (p - 0.25) / 0.25;
        if (bt < 0.0) bt = 0.0; if (bt > 1.0) bt = 1.0;
        double bAlpha = bt;
        double bScale = 1.25 - 0.25 * bt;                        // 1.25→1.0
        double by = (alertType == 2) ? 160.0 : 350.0;

        bool   useImg = (alertType == 2) ? hasAlertUnsealed : hasAlertSealed;
        GLuint bTex   = (alertType == 2) ? texAlertUnsealed : texAlertSealed;
        if (useImg) {
            double ar = (alertType == 2) ? alertUnsealedAR : alertSealedAR;
            double hh = 118.0 * bScale;
            double hw = hh * ar;
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, bTex);
            glColor4d(1.0, 1.0, 1.0, bAlpha);
            glBegin(GL_QUADS);
                glTexCoord2f(0.0, 0.0); glVertex2d(-hw, by + hh);
                glTexCoord2f(0.0, 1.0); glVertex2d(-hw, by - hh);
                glTexCoord2f(1.0, 1.0); glVertex2d( hw, by - hh);
                glTexCoord2f(1.0, 0.0); glVertex2d( hw, by + hh);
            glEnd();
            glDisable(GL_TEXTURE_2D);
        } else {
            // まだ日本語バナー画像が無い間は英字で代用（画像を置けば自動で切替）
            glDisable(GL_TEXTURE_2D);
            glColor4d(0.0, 0.0, 0.0, bAlpha * 0.5);
            glBegin(GL_QUADS);
                glVertex2d(-540.0, by + 50.0); glVertex2d(540.0, by + 50.0);
                glVertex2d(540.0, by - 45.0);  glVertex2d(-540.0, by - 45.0);
            glEnd();
            drawOutlinedText((alertType == 2) ? "SLASH UNSEALED!" : "SLASH SEALED!",
                             0.0, by - 18.0, 0.38 * bScale, true);
        }

        // 「スペースで開始」案内（登場アニメがほぼ終わってから点滅・押すまで待機）
        if (p > 0.8 && (gFrame / 20) % 2 == 0) {
            if (hasPressStart) { double ph = 52.0; drawHudImage(texPressStart, -ph * arPressStart / 2.0, 48.0, ph * arPressStart, ph); }
            else drawOutlinedText("PRESS SPACE TO START", 0.0, 45.0, 0.3, true);
        }
    }
}

// HUD用: 左端xLeft・中心yCenterに、幅w×高さhでテクスチャを1枚描く（日本語ラベル等）
void drawHudImage(GLuint tex, double xLeft, double yCenter, double w, double h) {
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4d(1.0, 1.0, 1.0, 1.0);
    double y0 = yCenter - h / 2.0, y1 = yCenter + h / 2.0;
    glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0); glVertex2d(xLeft,     y1);
        glTexCoord2f(0.0, 1.0); glVertex2d(xLeft,     y0);
        glTexCoord2f(1.0, 1.0); glVertex2d(xLeft + w, y0);
        glTexCoord2f(1.0, 0.0); glVertex2d(xLeft + w, y1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// PC版: プレイ中のHUD。壁の大型UIの代わりに、ゲーム画面向けの控えめな配置で描く
//   上部=半透明バーに 時間/目標・スコア・強化中 ／ 左下=コンパクトなHPバー
void drawPCHud()
{
    if (waveClearTimer > 0 || alertActive) return;   // クリア演出中・突入アラート中は隠す

    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);

    // 上部の半透明バー（文字を読みやすくする下地）
    glColor4d(0.0, 0.0, 0.0, 0.35);
    glBegin(GL_QUADS);
        glVertex2d(-550.0, 700.0); glVertex2d(550.0, 700.0);
        glVertex2d(550.0, 645.0);  glVertex2d(-550.0, 645.0);
    glEnd();

    // 左上: 時間 / 反射の残り数 / ボス戦
    if (currentWave == 2 && hasNokori && hasTai) {
        // WAVE2は「のこり N たい」を日本語ラベル画像＋数字テクスチャで表示
        int r = enemiesTargetWave2 - enemiesKilledInWave2;
        if (r < 0) r = 0; if (r > 9) r = 9;
        double y = 662.0, hgt = 30.0, x = -530.0;
        drawHudImage(texNokori, x, y, hgt * arNokori, hgt);   x += hgt * arNokori + 14.0;
        drawHudImage(texNumFloor[r], x, y, hgt * 0.72, hgt);  x += hgt * 0.72 + 14.0;
        drawHudImage(texTai, x, y, hgt * arTai, hgt);
    } else {
        char info[64];
        if (currentWave == 1) {
            sprintf(info, "TIME: %d", gameTimer / (int)fr);
        } else if (currentWave == 2) {
            int r = enemiesTargetWave2 - enemiesKilledInWave2;
            if (r < 0) r = 0;
            sprintf(info, "REFLECT: %d", r);
        } else {
            sprintf(info, "BOSS BATTLE");
        }
        drawOutlinedText(info, -530.0, 660.0, 0.28, false);
    }

    // 右上: スコア（中央揃えで右寄せ気味に配置）
    char sc[64];
    sprintf(sc, "SCORE: %d", score);
    drawOutlinedText(sc, 370.0, 660.0, 0.28, true);

    // 中央上: WAVE2は「こうげきできない！ガードではねかえそう」を常時案内。それ以外はパワーアップ表示
    if (currentWave == 2) {
        if (hasWave2Notice) {
            double nh = 30.0, nw = nh * arNotice;
            drawHudImage(texWave2Notice, -nw / 2.0, 665.0, nw, nh);
        } else {
            drawOutlinedText("SLASH SEALED! GUARD TO REFLECT", 0.0, 663.0, 0.18, true);
        }
    } else if (attackBuffTimer > 0) {
        drawOutlinedText("POWER UP!", 0.0, 660.0, 0.28, true);
    }

    // 左下: HPバー（コンパクト）
    float bw = 320.0, bh = 22.0, bx = -530.0, by = 18.0;
    glDisable(GL_TEXTURE_2D);
    glColor4d(0.0, 0.0, 0.0, 0.45);
    glBegin(GL_QUADS);
        glVertex2d(bx - 6, by - 6);      glVertex2d(bx + bw + 6, by - 6);
        glVertex2d(bx + bw + 6, by + bh + 6); glVertex2d(bx - 6, by + bh + 6);
    glEnd();
    float w = (float)playerHP / 100.0 * bw;
    if (w < 0) w = 0;
    if (w > bw) w = bw;
    if (playerHP <= 30) glColor4d(1.0, 0.15, 0.15, 0.95);  // 残りわずかで赤
    else                glColor4d(0.1, 0.95, 0.35, 0.95);
    glBegin(GL_QUADS);
        glVertex2d(bx, by);     glVertex2d(bx + w, by);
        glVertex2d(bx + w, by + bh); glVertex2d(bx, by + bh);
    glEnd();
    char hp[32];
    sprintf(hp, "HP %d", playerHP);
    drawOutlinedText(hp, bx, by + bh + 12.0, 0.2, false);
}

// PC版: 画面下の操作案内（タイトル/ルール/リザルトで表示。gFrameで点滅）
void drawPCPrompts()
{
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    bool blink = ((gFrame / 25) % 2 == 0);

    if (gameState == STATE_START) {
        // タイトル画像に開始案内（鎌を振ってゲームスタート）が焼き込み済みのため、ここでは重ねない（衝突防止）
    }
    else if (gameState == STATE_RULE) {
        // 開始案内のみ（画面下部に点滅）
        if (blink) {
            if (hasPressStart) { double ph = 60.0; drawHudImage(texPressStart, -ph * arPressStart / 2.0, 345.0, ph * arPressStart, ph); }
            else drawOutlinedText("PRESS SPACE TO START", 0.0, 345.0, 0.4, true);
        }
    }
    else if (gameState == STATE_RESULT) {
        // クリア/ゲームオーバー画像に案内（鎌を振ってタイトルへ）が焼き込み済みのため、ここでは重ねない（衝突防止）
    }
}


    void drawFloorScene() {
    glClear(GL_COLOR_BUFFER_BIT);

    // ▼▼▼ drawFloorScene 関数内の修正（元に戻す） ▼▼▼

    // 1. スタート画面 & 2. ルール画面
    if (gameState == STATE_START || gameState == STATE_RULE) {
        glEnable(GL_TEXTURE_2D);
        
        // タイトルは鎌の魔法陣(title_floor)、ルールはステージ1背景（※展示版の床投影のみ。PC版のタイトルはtitle.png）
        if (gameState == STATE_START && hasTitleFloor) glBindTexture(GL_TEXTURE_2D, texTitleFloor);
        else glBindTexture(GL_TEXTURE_2D, texBg[0]);

        glColor3d(1.0, 1.0, 1.0);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-550.0, 700.0);
            glTexCoord2f(0.0, 1.0); glVertex2d(-550.0, 0.0);
            glTexCoord2f(1.0, 1.0); glVertex2d(550.0, 0.0);
            glTexCoord2f(1.0, 0.0); glVertex2d(550.0, 700.0);
        glEnd();
        
        // ★ここは残す：「Wait for Game Start...」の文字表示コード
        if (gameState == STATE_RULE && sceneTransitionTimer > 0) {
             // ...（以前書いた文字表示のコードはそのまま残してください）...
        }

        glDisable(GL_TEXTURE_2D);
        glFlush();
        return;
    }

    // 3. リザルト画面
    if (gameState == STATE_RESULT) {
        glEnable(GL_TEXTURE_2D);
        
        // クリアとゲームオーバーで床の映像を分ける
        glBindTexture(GL_TEXTURE_2D, isGameOver ? texGameOverFloor : texClearFloor);

        glColor3d(1.0, 1.0, 1.0);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-550.0, 700.0);
            glTexCoord2f(0.0, 1.0); glVertex2d(-550.0, 0.0);
            glTexCoord2f(1.0, 1.0); glVertex2d(550.0, 0.0);
            glTexCoord2f(1.0, 0.0); glVertex2d(550.0, 700.0);
        glEnd();

        glDisable(GL_TEXTURE_2D);
        glFlush();
        return;
    }

    // --- 4. ゲーム本編 (STATE_PLAY) ---

    glPushMatrix(); // 現在の座標を保存
    if (playerDamageTimer > 0) {
        // -15〜15の間でランダムに画面をずらす（ガガガッと揺れる演出）
        double shakeX = (rand() % 30) - 15.0;
        double shakeY = (rand() % 30) - 15.0;
        glTranslated(shakeX, shakeY, 0.0);
    }

    glEnable(GL_TEXTURE_2D);
    glColor3d(1.0, 1.0, 1.0);
    
    // WAVE（ステージ）に応じて背景画像を切り替え
    if (currentWave == 1) {
        glBindTexture(GL_TEXTURE_2D, texBg[0]); // Stage 1
    } else if (currentWave == 2) {
        glBindTexture(GL_TEXTURE_2D, texBg[1]); // Stage 2
    } else {
        glBindTexture(GL_TEXTURE_2D, texBg[2]); // Stage 3 (Boss)
    }

    
    glBegin(GL_QUADS);
    // 画面全体にテクスチャを貼り付け (-550 ~ 550, 0 ~ 700)
    glTexCoord2f(0.0, 0.0); glVertex2d(-550.0, 700.0); // 左上
    glTexCoord2f(0.0, 1.0); glVertex2d(-550.0, 0.0);   // 左下
    glTexCoord2f(1.0, 1.0); glVertex2d(550.0, 0.0);    // 右下
    glTexCoord2f(1.0, 0.0); glVertex2d(550.0, 700.0);  // 右上
    glEnd();
    glDisable(GL_TEXTURE_2D);

   glEnable(GL_BLEND); // 半透明を有効化
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // ★ここの「0.5」の数字を変えると暗さを調整できます (0.0=透明 〜 1.0=真っ黒)
    glColor4d(0.0, 0.0, 0.0, 0.2); 

    glBegin(GL_QUADS);
    glVertex2d(-550.0, 700.0);
    glVertex2d(-550.0, 0.0);
    glVertex2d(550.0, 0.0);
    glVertex2d(550.0, 700.0);
    glEnd();

    glColor4d(1.0, 1.0, 1.0, 1.0);

    // ▼▼▼ 【新機能】ボス戦ヘルプスポット（WAVE3のみ・床の右下に光るスポットを置く） ▼▼▼
    //   プレイヤーが踏むと充填ゲージがたまり、スポットが明るく光る（踏んだ手応え）。
    //   （壁には別途ボスの倒し方＝rule2 が大きく表示される：drawWallScene 側）
    // WAVE3=金マーク(両モード) / WAVE1・2=石マーク(PC版のみ)。踏むとそのWAVEの遊び方を全画面表示。
    bool showSpot = (waveClearTimer <= 0) &&
                    (currentWave == 3 || (pcMode && (currentWave == 1 || currentWave == 2)));
    if (showSpot) {
        // 判定ゾーン＝床の「右下」。ボスは上中央(0,400)に居るので下の隅は安全。
        bool onSpot = (footNum > 0 && footPos[0][0] > 320.0 && footPos[0][1] < 180.0);
        float pulse = 0.5f + 0.5f * sinf(gFrame * 0.12f);   // ゆっくり明滅
        float sx = 445.0f, sy = 115.0f;                     // 床・右下の位置
        float half = onSpot ? 70.0f : 55.0f;                // 踏むと少し大きくなる（大きすぎたので縮小）

        // WAVE3=金の魔法陣 / WAVE2=金コイン / WAVE1=石（未生成なら魔法陣で代用）
        GLuint markTex = (currentWave == 3) ? texHelpSpot
                       : (currentWave == 2) ? (hasHelpGold  ? texHelpSpotGold  : texHelpSpot)
                       :                       (hasHelpPlain ? texHelpSpotPlain : texHelpSpot);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, markTex);
        float br = onSpot ? 1.0f : (0.60f + 0.25f * pulse); // 明るさ（踏むと最大）
        glColor4d(br, br, br, 1.0);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(sx - half, sy + half);
            glTexCoord2f(0.0, 1.0); glVertex2d(sx - half, sy - half);
            glTexCoord2f(1.0, 1.0); glVertex2d(sx + half, sy - half);
            glTexCoord2f(1.0, 0.0); glVertex2d(sx + half, sy + half);
        glEnd();
        glDisable(GL_TEXTURE_2D);

        glColor4d(1.0, 1.0, 1.0, 1.0); // 色を元に戻す
    }
    // ▲▲▲ ヘルプスポットここまで ▲▲▲

    // ▼▼▼ 追加: アイテム描画 (敵より奥に描画するためここで書く) ▼▼▼
    glEnable(GL_TEXTURE_2D);
    glColor3d(1.0, 1.0, 1.0);
    for (int i=0; i<MAX_ITEMS; i++) {
        if (items[i].active) {
            
            // ★サイズをタイプによって設定
            float itemHalfSize = (items[i].type == 0) ? 40.0 : 50.0; // 回復(40x40) or パワーアップ(60x60)
            
            glBindTexture(GL_TEXTURE_2D, (items[i].type == 0) ? texItemHeal : texItemBuff);
            glPushMatrix();
            glTranslated(items[i].x, items[i].y, 0.0);
            
            // アイテム画像描画 
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-itemHalfSize, itemHalfSize);
            glTexCoord2f(0.0, 1.0); glVertex2d(-itemHalfSize, -itemHalfSize);
            glTexCoord2f(1.0, 1.0); glVertex2d(itemHalfSize, -itemHalfSize);
            glTexCoord2f(1.0, 0.0); glVertex2d(itemHalfSize, itemHalfSize);
            glEnd();
            glPopMatrix();
        }
    }
    glDisable(GL_TEXTURE_2D);

    // --- 足跡の描画 ---
    for (int i = 0; i < footprintCount; i++) {
        int index = (footprintIndex + MAX_FOOTPRINTS - 1 - i) % MAX_FOOTPRINTS;
        float alpha = 0.3 * (1.0 - (float)i / footprintCount);
        if (alpha < 0) alpha = 0;
        
        glColor4d(0.0, 1.0, 1.0, alpha); 

        glPushMatrix();
        glTranslated(footprintPos[index][0], footprintPos[index][1], 0.0);
        int points = 10; 
        double radius = 5.0;
        glBegin(GL_POLYGON);
        for (int j = 0; j < points; j++) {
            double angle = 2.0 * M_PI * j / points;
            glVertex2d(cos(angle) * radius, sin(angle) * radius);
        }
        glEnd();
        glPopMatrix();
    }

    // ボス近接衝撃波の炎（地面に広がる・加算合成で黒背景は消える）
    if (bossShockTimer > 0) {
        double t  = 1.0 - (double)bossShockTimer / 24.0;             // 0→1で広がる
        double sz = 120.0 + 340.0 * t;
        double al = (bossShockTimer > 6) ? 1.0 : (double)bossShockTimer / 6.0;
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);                           // 加算＝炎が光り黒は透ける
        glBindTexture(GL_TEXTURE_2D, texBossShock);
        glColor4d(1.0, 1.0, 1.0, al);
        glPushMatrix();
        glTranslated(bossShockX, bossShockY, 0.0);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-sz,  sz);
            glTexCoord2f(0.0, 1.0); glVertex2d(-sz, -sz);
            glTexCoord2f(1.0, 1.0); glVertex2d( sz, -sz);
            glTexCoord2f(1.0, 0.0); glVertex2d( sz,  sz);
        glEnd();
        glPopMatrix();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);           // 元に戻す
        glDisable(GL_TEXTURE_2D);
        glColor4d(1.0, 1.0, 1.0, 1.0);
    }
    

// --- 敵の描画 ---
    for(int i=0; i<MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            glEnable(GL_TEXTURE_2D);

            // ▼▼▼ ボスの画像切り替えロジック (安定化・復元版) ▼▼▼
            if (enemies[i].type == 1) { 
                
                // 1. 最優先: ダメージ中
                if (enemies[i].hitTimer > 0) { 
                    glBindTexture(GL_TEXTURE_2D, texBossDamaged); 
                } 
                // 2. 防御中
                else if (bossIsDefending) { 
                    glBindTexture(GL_TEXTURE_2D, texBossDefense); 
                }
                // 3. 溜め中 (爆発)
                else if (enemies[i].isCharging && enemies[i].attackPattern == 2) {
                     glBindTexture(GL_TEXTURE_2D, texBossExpCharge); 
                }
                // 4. 溜め中 (ビーム)
                else if (enemies[i].isCharging) {
                     glBindTexture(GL_TEXTURE_2D, texBossCharge); 
                }
                // 5. 発動ポーズの持続表示 (爆発後の叩きつけ)
                else if (enemies[i].attackTimer > 0 && enemies[i].attackPattern == 0) {
                     glBindTexture(GL_TEXTURE_2D, texBossExpFire); 
                }
                // 6. 通常時 (上記すべてに該当しない場合)
                else { 
                    // ★修正: 初期描画と通常インターバル時
                    glBindTexture(GL_TEXTURE_2D, texBoss); 
                }
            } else { // 雑魚敵なら
                glBindTexture(GL_TEXTURE_2D, enemies[i].textureID); 
            }
            // ▲▲▲ ボスの画像切り替えロジック終わり ▲▲▲
            
            // 色の制御 (点滅演出と被弾時の赤色化)
            // 【新機能】近接爆発の予兆: 近づきすぎ警告中はボスが赤く点滅（最優先で表示）
            if (enemies[i].type == 1 && enemies[i].closeRangeTimer > 90) {
                if ((enemies[i].closeRangeTimer / 4) % 2 == 0) glColor3d(1.0, 0.2, 0.2); // 危険な赤
                else glColor3d(1.0, 1.0, 1.0);
            }
            // 溜め中(爆発)の本体点滅演出を追加
            else if (enemies[i].type == 1 && enemies[i].isCharging && enemies[i].attackPattern == 2) {
                // 点滅速度: chargeTimer/5 でオンオフ
                if ((enemies[i].chargeTimer / 5) % 2 == 0) { 
                    glColor3d(1.0, 0.5, 0.5); // 赤っぽく光る
                } else {
                    glColor3d(1.0, 1.0, 1.0); // 通常色 (テクスチャ色)
                }
            }
            // ダメージ時の赤色化
            else if (enemies[i].hitTimer > 0) glColor3d(1.0, 0.0, 0.0);
            // 防御中の青色化
            else if (bossIsDefending && enemies[i].type == 1) glColor3d(0.7, 0.7, 1.0);
            // 通常色
            else glColor3d(1.0, 1.0, 1.0);

        

            glPushMatrix();
            glTranslated(enemies[i].x, enemies[i].y, 0.0);
            
            // サイズ決定
            float size = (enemies[i].type == 1) ? 400.0 : 180.0;
            float halfSize = size / 2.0;

            // 敵本体描画
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-halfSize, halfSize);  // 左上
            glTexCoord2f(0.0, 1.0); glVertex2d(-halfSize, -halfSize); // 左下
            glTexCoord2f(1.0, 1.0); glVertex2d(halfSize, -halfSize);  // 右下
            glTexCoord2f(1.0, 0.0); glVertex2d(halfSize, halfSize);   // 右上
            glEnd();
            glPopMatrix();
            glDisable(GL_TEXTURE_2D); 

           // ▼▼▼ 2. ボス溜めエフェクト描画 (パターン1のビーム溜め時のみ) ▼▼▼
            if (enemies[i].type == 1 && enemies[i].isCharging && enemies[i].attackPattern == 1) {
                // 点滅演出
                if ((enemies[i].chargeTimer / 5) % 2 == 0) {
                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, texBossBeamC); // 常にビームチャージエフェクトを使う
                    glColor3d(1.0, 1.0, 1.0);
                    
                    glPushMatrix();
                    glTranslated(enemies[i].x, enemies[i].y, 0.0);
                    glBegin(GL_QUADS);
                    // ボスの手元に合わせたサイズ (溜め中画像に合わせたサイズ)
                    glTexCoord2f(0.0, 0.0); glVertex2d(-200.0, 200.0);
                    glTexCoord2f(0.0, 1.0); glVertex2d(-200.0, -100.0);
                    glTexCoord2f(1.0, 1.0); glVertex2d(200.0, -100.0);
                    glTexCoord2f(1.0, 0.0); glVertex2d(200.0, 200.0);
                    glEnd();
                    glPopMatrix();
                    glDisable(GL_TEXTURE_2D);
                }
            }
            // ▲▲▲ 溜めエフェクトブロック修正終わり ▲▲▲

            // HPバー描画
            glPushMatrix();
            float barOffset = (enemies[i].type == 1) ? 100.0 : 80.0; // 位置調整
            glTranslated(enemies[i].x, enemies[i].y + barOffset, 0.0);
            
            // 背景(黒)
            glColor3d(0.2, 0.2, 0.2);
            glBegin(GL_QUADS);
            glVertex2d(-30.0, 0.0); glVertex2d(30.0, 0.0);
            glVertex2d(30.0, 8.0);  glVertex2d(-30.0, 8.0);
            glEnd();
            
            // 現在HP(赤)
            glColor3d(1.0, 0.0, 0.0);
            float hpPercent = (float)enemies[i].hp / enemies[i].maxHp;
            if (hpPercent < 0) hpPercent = 0;
            glBegin(GL_QUADS);
            glVertex2d(-30.0, 0.0); 
            glVertex2d(-30.0 + (60.0 * hpPercent), 0.0);
            glVertex2d(-30.0 + (60.0 * hpPercent), 8.0); 
            glVertex2d(-30.0, 8.0);
            glEnd();
            glPopMatrix();
        }
    }

// display 関数内の「弾の描画」ループ部分を以下に書き換え
// display 関数内の「弾の描画」ループ部分
for (int i = 0; i < MAX_BULLETS; i++) {
    if (bullets[i].active) {
        glPushMatrix();
        glTranslated(bullets[i].x, bullets[i].y, 0.0);
        
        // 進行方向の角度を計算
        double angleDeg = atan2(bullets[i].vy, bullets[i].vx) * 180.0 / M_PI;
        
        glEnable(GL_TEXTURE_2D);
        glColor3d(1.0, 1.0, 1.0);

        // --- 敵の弾 (isEnemyBullet) ---
        if (bullets[i].isEnemyBullet) {
            
            // ★描画テクスチャの選択とサイズ定義
            GLuint currentTex = 0;
            float w = 50.0, h = 50.0; // デフォルトサイズ (通常弾のサイズ)
            
            if (bullets[i].type == 1) { 
                // 【ビーム】
                currentTex = texBossBeamF;
                w = 200.0; h = 80.0; // ★修正: ビーム弾専用の大型サイズ
            } 
            else if (bullets[i].type == 2) {
                // 【投石弾 (爆発)】
                // 弾に設定されたテクスチャIDを使用
                currentTex = bullets[i].textureID; 
                w = 60.0; h = 60.0; // 投石弾サイズ
            }
            else { 
                // 【通常弾】
                currentTex = texBullEnemy;
                w = 100.0; h = 70.0; // 通常弾サイズ
            }
            
            // ▼▼▼ ここから描画処理 ▼▼▼
            glBindTexture(GL_TEXTURE_2D, currentTex);
            glPushMatrix();
            glRotated(angleDeg, 0, 0, 1);
            
            // ★描画 (サイズ変数を適用)
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-w/2, h/2);   // 左上
            glTexCoord2f(0.0, 1.0); glVertex2d(-w/2, -h/2);  // 左下
            glTexCoord2f(1.0, 1.0); glVertex2d(w/2, -h/2);   // 右下
            glTexCoord2f(1.0, 0.0); glVertex2d(w/2, h/2);    // 右上
            glEnd();
            
            glPopMatrix();

        } else { // --- 反射弾 ---
            glBindTexture(GL_TEXTURE_2D, texBullRef);
            // ... (反射弾の描画ロジックはそのまま) ...
            glPushMatrix();
            glRotated(angleDeg, 0, 0, 1);
            float w_ref = 100.0, h_ref = 70.0; // サイズ調整
            
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-w_ref/2, h_ref/2);
            glTexCoord2f(0.0, 1.0); glVertex2d(-w_ref/2, -h_ref/2);
            glTexCoord2f(1.0, 1.0); glVertex2d(w_ref/2, -h_ref/2);
            glTexCoord2f(1.0, 0.0); glVertex2d(w_ref/2, h_ref/2);
            glEnd();
            glPopMatrix();
        }
        
        glDisable(GL_TEXTURE_2D);
        glPopMatrix();
        
    }
}

    // --- プレイヤーとエフェクト
   if (footNum > 0) {
        glPushMatrix();
        glTranslated(footPos[0][0], footPos[0][1], 0.0);
        glRotated(playerAngle - 90.0, 0.0, 0.0, 1.0);

        // 0. PC版の現在地マーカー（アバター画像が無いので、位置と向きを常時可視化。LiDAR/展示時は実体があるので出さない）
        if (pcMode) {
            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            double pulse = 0.5 + 0.5 * sin((double)gFrame * 0.15);
            // 接地の影（明るい床でもリングが目立つ下地）
            glColor4d(0.0, 0.0, 0.0, 0.25);
            glBegin(GL_POLYGON);
            for (int j = 0; j < 24; j++) { double a = 2.0*M_PI*j/24; glVertex2d(cos(a)*36.0, sin(a)*36.0); }
            glEnd();
            // 内側の淡いディスク
            glColor4d(0.20, 0.95, 1.0, 0.16 + 0.10*pulse);
            glBegin(GL_POLYGON);
            for (int j = 0; j < 24; j++) { double a = 2.0*M_PI*j/24; glVertex2d(cos(a)*30.0, sin(a)*30.0); }
            glEnd();
            // 光るリング（現在地）
            glColor4d(0.25, 1.0, 1.0, 0.55 + 0.35*pulse);
            glLineWidth(3.0);
            glBegin(GL_LINE_LOOP);
            for (int j = 0; j < 32; j++) { double a = 2.0*M_PI*j/32; glVertex2d(cos(a)*34.0, sin(a)*34.0); }
            glEnd();
            glLineWidth(1.0);
            // 向き矢印（ローカル+Y＝向いている方向）
            glColor4d(1.0, 1.0, 0.4, 0.85);
            glBegin(GL_TRIANGLES);
            glVertex2d(0.0, 74.0); glVertex2d(-16.0, 46.0); glVertex2d(16.0, 46.0);
            glEnd();
            glColor4d(1.0, 1.0, 1.0, 1.0);
        }

        // 1. 横振り
        if (sweepEffectTimer > 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texEffAtkH);
            glColor3d(1.0, 1.0, 1.0);
            
            glPushMatrix();
            // ★修正: 向きと座標　いちのみ変更した
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-120.0, 150.0);
            glTexCoord2f(0.0, 1.0); glVertex2d(-120.0, -130.0);
            glTexCoord2f(1.0, 1.0); glVertex2d(120.0, -130.0);
            glTexCoord2f(1.0, 0.0); glVertex2d(120.0, 150.0);
            
        

        
            
            glEnd();
            glPopMatrix();
            glDisable(GL_TEXTURE_2D);
        }

        // 2. 縦振り
        if (swingDownEffectTimer > 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texEffAtkV);
            glColor3d(1.0, 1.0, 1.0);

            glPushMatrix();
            // ★修正: 向きと座標
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-50.0, 600.0);
            glTexCoord2f(0.0, 1.0); glVertex2d(-50.0, 50.0);
            glTexCoord2f(1.0, 1.0); glVertex2d(50.0, 50.0);
            glTexCoord2f(1.0, 0.0); glVertex2d(50.0, 600.0);
            glEnd();
            glPopMatrix();
            glDisable(GL_TEXTURE_2D);
        }

        // 3. 防御シールド (反射成功で発光！) ※攻撃中は出さない（攻撃中に防御がワープして見えるバグの修正）
        if ((isDefending || defenseEffectTimer > 0) && sweepEffectTimer <= 0 && swingDownEffectTimer <= 0) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, texEffDef);
            
            if (reflectFlashTimer > 0) {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE); // 発光
                glColor4d(0.5, 1.0, 1.0, 1.0);
            } else {
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // 通常
                glColor4d(1.0, 1.0, 1.0, 0.8);
            }

            glPushMatrix();
            glTranslated(0.8, 10.0, 0.0);
            // ★修正: 向きと座標 位置のみ変更
            glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-90.0, 80.0);
            glTexCoord2f(0.0, 1.0); glVertex2d(-90.0, -50.0);
            glTexCoord2f(1.0, 1.0); glVertex2d(90.0, -50.0);
            glTexCoord2f(1.0, 0.0); glVertex2d(90.0, 80.0);
            glEnd();
            glPopMatrix();
            
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); 
            glDisable(GL_TEXTURE_2D);
        }
        glPopMatrix();
    }

    if (playerDamageTimer > 0) {
        glEnable(GL_BLEND);
        // ダメージ直後は濃く、時間が経つと薄くなる赤色
        float alpha = (float)playerDamageTimer / 40.0; // 最大0.5くらいの濃さ
        glColor4d(1.0, 0.0, 0.0, alpha);
        
        glDisable(GL_TEXTURE_2D); // テクスチャなしで描画
        glBegin(GL_QUADS);
        glVertex2d(-550.0, 700.0);
        glVertex2d(-550.0, 0.0);
        glVertex2d(550.0, 0.0);
        glVertex2d(550.0, 700.0);
        glEnd();
    }

    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);

    // 1. 攻撃UP (オレンジ色)
    if (attackBuffTimer > 0) {
        bool showFilter = true;
        // 残り3秒(150フレーム)以下なら点滅させる
        if (attackBuffTimer < 150) {
            // 時間が減るほど点滅が早くなる計算
            int blinkSpeed = (attackBuffTimer / 30) + 2; 
            if ((attackBuffTimer / blinkSpeed) % 2 == 0) showFilter = false;
        }

        if (showFilter) {
            glColor4d(1.0, 0.5, 0.0, 0.2); // 薄いオレンジ (透明度0.2)
            glBegin(GL_QUADS);
            glVertex2d(-550.0, 700.0); glVertex2d(-550.0, 0.0);
            glVertex2d(550.0, 0.0);    glVertex2d(550.0, 700.0);
            glEnd();
        }
    }

    // 2. 回復演出 (緑色フラッシュ)
    if (healEffectTimer > 0) {
        float alpha = (float)healEffectTimer / 30.0; // 徐々に消える
        glColor4d(0.0, 1.0, 0.0, alpha); // 緑色
        glBegin(GL_QUADS);
        glVertex2d(-550.0, 700.0); glVertex2d(-550.0, 0.0);
        glVertex2d(550.0, 0.0);    glVertex2d(550.0, 700.0);
        glEnd();
    }

    if (waveClearTimer > 0) {
        // 1. 紙吹雪の描画
        glDisable(GL_TEXTURE_2D);
        for (int i = 0; i < MAX_CONFETTI; i++) {
            if (confetti[i].active) {
                glColor3d(confetti[i].r, confetti[i].g, confetti[i].b);
                glPushMatrix();
                glTranslated(confetti[i].x, confetti[i].y, 0.0);
                // 小さな四角形を描画
                float s = confetti[i].size;
                glBegin(GL_QUADS);
                    glVertex2d(-s, s);
                    glVertex2d(-s, -s);
                    glVertex2d(s, -s);
                    glVertex2d(s, s);
                glEnd();
                glPopMatrix();
            }
        }

        // 2. 黒い半透明ボックス (文字を見やすくするため・上下2段ぶんに拡大)
        glEnable(GL_BLEND);
        glColor4d(0.0, 0.0, 0.0, 0.6); // 薄い黒
        glBegin(GL_QUADS);
            glVertex2d(-550.0, 520.0);
            glVertex2d(-550.0, 110.0);
            glVertex2d(550.0, 110.0);
            glVertex2d(550.0, 520.0);
        glEnd();

        // 3. 「STAGE CLEAR!」の金色画像（上段）
        glEnable(GL_TEXTURE_2D);
        glColor4d(1.0, 1.0, 1.0, 1.0);
        glBindTexture(GL_TEXTURE_2D, texStageClear);
        float scw = 420.0, sch = 84.0; // STAGE CLEAR! の半幅・半高（約5:1）
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-scw, 410.0 + sch);
            glTexCoord2f(0.0, 1.0); glVertex2d(-scw, 410.0 - sch);
            glTexCoord2f(1.0, 1.0); glVertex2d( scw, 410.0 - sch);
            glTexCoord2f(1.0, 0.0); glVertex2d( scw, 410.0 + sch);
        glEnd();

        // 4. 「NEXT STAGE」金色画像（下段・左）＋ カウントダウン数字（下段・右）
        //    旧：drawText("NEXT STAGE in X") の線フォントから、金色画像＋金色数字に差し替え
        int remainSec = waveClearTimer / 50 + 1; // 残り秒（5→1）
        float nsCx = -70.0, nsCy = 250.0, nsW = 215.0, nsH = 43.0; // NEXT STAGE 画像
        glBindTexture(GL_TEXTURE_2D, texNextStage);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(nsCx - nsW, nsCy + nsH);
            glTexCoord2f(0.0, 1.0); glVertex2d(nsCx - nsW, nsCy - nsH);
            glTexCoord2f(1.0, 1.0); glVertex2d(nsCx + nsW, nsCy - nsH);
            glTexCoord2f(1.0, 0.0); glVertex2d(nsCx + nsW, nsCy + nsH);
        glEnd();
        // カウントダウンの数字（床コンテキストの金色数字スプライト）
        int cdDigit = remainSec % 10;
        float dCx = 215.0, dCy = 250.0, dW = 32.0, dH = 46.0;
        glBindTexture(GL_TEXTURE_2D, texNumFloor[cdDigit]);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(dCx - dW, dCy + dH);
            glTexCoord2f(0.0, 1.0); glVertex2d(dCx - dW, dCy - dH);
            glTexCoord2f(1.0, 1.0); glVertex2d(dCx + dW, dCy - dH);
            glTexCoord2f(1.0, 0.0); glVertex2d(dCx + dW, dCy + dH);
        glEnd();

        // スキップ案内（点滅・下段。スペースでクリア演出を飛ばせる）
        if (hasSkipPrompt && (gFrame / 25) % 2 == 0) {
            double hh = 21.0, hw = hh * arSkipPrompt;
            glColor4d(1.0, 1.0, 1.0, 1.0);
            glBindTexture(GL_TEXTURE_2D, texSkipPrompt);
            glBegin(GL_QUADS);
                glTexCoord2f(0.0, 0.0); glVertex2d(-hw, 148.0 + hh);
                glTexCoord2f(0.0, 1.0); glVertex2d(-hw, 148.0 - hh);
                glTexCoord2f(1.0, 1.0); glVertex2d( hw, 148.0 - hh);
                glTexCoord2f(1.0, 0.0); glVertex2d( hw, 148.0 + hh);
            glEnd();
        }
        glDisable(GL_TEXTURE_2D);

        glDisable(GL_BLEND);
    }
    // --- UI（スコア・HP・WAVE等）は壁ウィンドウに集約したので、床からは描かない（2画面の役割整理）---
    // 床＝ゲーム本体（敵・弾・プレイヤー・演出）に専念し、情報表示は壁が担当する

    // --- デバッグ表示（'d'キーでトグル）: UIと重ならない左下に半透明パネルで出す ---
    if (debugMode) {
        glDisable(GL_TEXTURE_2D);
        // 半透明の黒パネル（文字を読みやすく・UIと分離）
        glEnable(GL_BLEND);
        glColor4d(0.0, 0.0, 0.0, 0.6);
        glBegin(GL_QUADS);
            glVertex2d(-550.0, 215.0);
            glVertex2d(-550.0, 55.0);
            glVertex2d(-175.0, 55.0);
            glVertex2d(-175.0, 215.0);
        glEnd();
        char dbg[128];
        float dy = 190.0;
        sprintf(dbg, "[DEBUG] input:%s", useKeyboardMode ? "KEYBOARD" : "M5STICK");
        drawText(dbg, -545.0, dy); dy -= 30;
        sprintf(dbg, "acc : %.2f %.2f %.2f", val[0], val[1], val[2]);
        drawText(dbg, -545.0, dy); dy -= 30;
        sprintf(dbg, "gyro: %.1f %.1f %.1f", val[3], val[4], val[5]);
        drawText(dbg, -545.0, dy); dy -= 30;
        sprintf(dbg, "foot:%d (%.0f,%.0f) ang:%.0f", footNum, footPos[0][0], footPos[0][1], playerAngle);
        drawText(dbg, -545.0, dy); dy -= 30;
        sprintf(dbg, "def:%d atk:%d wave:%d avg:%.1f", isDefending, canAttack, currentWave, avgMove);
        drawText(dbg, -545.0, dy);
    }

    glPopMatrix(); // 655行のglPushMatrix(画面揺れ用)に対応。pop欠落で座標がドリフトしていたバグの修正
    glFlush();
}


// 縁取り・中央揃え対応の文字描画関数 (修正版2：位置ズレ修正)
void drawOutlinedText(const char* str, float x, float y, float scale, bool centered) {
    glPushMatrix();

    // 線を滑らかにする（ギザギザ＝「気持ち悪い」の主因を解消）
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // --- 1. 中央揃えの計算 ---
    float width = 0;
    if (centered) {
        int len = glutStrokeLength(GLUT_STROKE_ROMAN, (const unsigned char*)str);
        width = (float)len * scale;
        x -= width / 2.0;
    }

    // --- 2. 描画開始位置へ移動 ---
    glTranslated(x, y, 0.0);
    glScaled(scale, scale, 1.0);

    // --- 3. 黒い縁取り(極太)を描く ---
    glPushMatrix();
    glLineWidth(9.0); // 5.0→9.0 でくっきり
    glColor3d(0.0, 0.0, 0.0);
    for (const char* p = str; *p; p++) {
        glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
    }
    glPopMatrix();

    // --- 4. 白い文字(普通)を重ねる ---
    glTranslated(0.0, 0.0, 1.0);
    glLineWidth(4.0); // 2.5→4.0 で太く
    glColor3d(1.0, 1.0, 1.0);
    for (const char* p = str; *p; p++) {
        glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
    }

    glPopMatrix();
    glLineWidth(1.0);
    glDisable(GL_LINE_SMOOTH);
}

// 数字を金色画像で描画（cx,cyを中央に、1桁の幅digitWで横並び）
void drawNumber(int value, float cx, float cy, float digitW) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    int n = (int)strlen(buf);
    float totalW = n * digitW;
    float startX = cx - totalW / 2.0;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4d(1.0, 1.0, 1.0, 1.0);
    for (int i = 0; i < n; i++) {
        int d = buf[i] - '0';
        if (d < 0 || d > 9) continue;
        glBindTexture(GL_TEXTURE_2D, texNum[d]);
        float left = startX + i * digitW;
        float h = digitW; // 正方形素材
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(left,          cy + h / 2);
            glTexCoord2f(0.0, 1.0); glVertex2d(left,          cy - h / 2);
            glTexCoord2f(1.0, 1.0); glVertex2d(left + digitW, cy - h / 2);
            glTexCoord2f(1.0, 0.0); glVertex2d(left + digitW, cy + h / 2);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

// ランク文字を画像で描画（cx,cyを中央に、size四方）
void drawRank(char grade, float cx, float cy, float size) {
    int idx = -1;
    switch (grade) {
        case 'S': idx = 0; break;
        case 'A': idx = 1; break;
        case 'B': idx = 2; break;
        case 'C': idx = 3; break;
        case 'D': idx = 4; break;
    }
    if (idx < 0) return;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4d(1.0, 1.0, 1.0, 1.0);
    glBindTexture(GL_TEXTURE_2D, texRankImg[idx]);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0); glVertex2d(cx - size / 2, cy + size / 2);
        glTexCoord2f(0.0, 1.0); glVertex2d(cx - size / 2, cy - size / 2);
        glTexCoord2f(1.0, 1.0); glVertex2d(cx + size / 2, cy - size / 2);
        glTexCoord2f(1.0, 0.0); glVertex2d(cx + size / 2, cy + size / 2);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// 壁側ディスプレイの描画ロジック (文字ズレ修正版)
void drawWallScene() {
    glClear(GL_COLOR_BUFFER_BIT); 
    glLoadIdentity(); 

    // ▼▼▼ 1. ダメージ演出 (画面揺れ) ▼▼▼
    if (playerDamageTimer > 0) {
        double shakeX = (rand() % 40) - 20.0;
        double shakeY = (rand() % 40) - 20.0;
        glTranslated(shakeX, shakeY, 0.0);
    }

    // ==========================================
    // 背景描画
    // ==========================================
    glEnable(GL_TEXTURE_2D);
    glColor3d(1.0, 1.0, 1.0);

    if (gameState == STATE_START) glBindTexture(GL_TEXTURE_2D, texTitle);
    else if (gameState == STATE_RULE) glBindTexture(GL_TEXTURE_2D, texRule1); // 最初のルールは遊び方(rule1)のみ。ボス攻撃(rule2)はボス戦の床ヘルプスポットで表示
    else if (gameState == STATE_RESULT) {
        if (isGameOver) glBindTexture(GL_TEXTURE_2D, texGameOver);
        else glBindTexture(GL_TEXTURE_2D, texClear);
    }
    else { // STATE_PLAY
        if (currentWave == 1) glBindTexture(GL_TEXTURE_2D, texWallWave1);
        else if (currentWave == 2) glBindTexture(GL_TEXTURE_2D, texWallWave2);
        else glBindTexture(GL_TEXTURE_2D, texWallWave3);
    }

    glBegin(GL_QUADS);
        glTexCoord2f(0.0, 0.0); glVertex2d(-550.0, 700.0);
        glTexCoord2f(0.0, 1.0); glVertex2d(-550.0, 0.0);
        glTexCoord2f(1.0, 1.0); glVertex2d(550.0, 0.0);
        glTexCoord2f(1.0, 0.0); glVertex2d(550.0, 700.0);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // ▼▼▼ 2. ダメージ演出 (赤色フラッシュ) ▼▼▼
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (playerDamageTimer > 0) {
        float alpha = (float)playerDamageTimer / 30.0; 
        if(alpha > 0.6) alpha = 0.6; 
        glColor4d(1.0, 0.0, 0.0, alpha);
        glBegin(GL_QUADS);
            glVertex2d(-600.0, 800.0);
            glVertex2d(-600.0, -100.0);
            glVertex2d(600.0, -100.0);
            glVertex2d(600.0, 800.0);
        glEnd();
    }

    // ==========================================
    // UI 描画 (プレイ中のみ。クリア演出中は動く数字を隠して演出に集中)
    // ==========================================
    if (gameState == STATE_PLAY && waveClearTimer <= 0) {

        // -------------------------------------------------
        // A. 左上: タイマー / 目標
        // -------------------------------------------------
        char infoText[100];
        if (currentWave == 1) {
            sprintf(infoText, "TIME: %d", gameTimer / (int)fr);
        } else if (currentWave == 2) {
            int remains = enemiesTargetWave2 - enemiesKilledInWave2;
            if(remains < 0) remains = 0;
            sprintf(infoText, "TARGET: %d", remains);
        } else {
            sprintf(infoText, "BOSS BATTLE");
        }

        glPushMatrix();
        glTranslated(-520.0, 620.0, 0.0);
        glScaled(0.4, 0.4, 1.0);
        
        // ★修正: 黒文字(フチ)を描く処理を Push/Pop で囲む
        glPushMatrix(); // 位置を記憶
        glLineWidth(5.0);
        glColor3d(0.0, 0.0, 0.0);
        for(char* p = infoText; *p; p++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
        glPopMatrix(); // 位置を戻す (これで次の白文字がズレない)
        
        // 白文字
        glLineWidth(3.0);
        glTranslated(0.0, 0.0, 0.0); 
        glColor3d(1.0, 1.0, 1.0);
        for(char* p = infoText; *p; p++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
        glPopMatrix();


        // -------------------------------------------------
        // B. 右上: スコア
        // -------------------------------------------------
        char scoreText[100];
        sprintf(scoreText, "SCORE: %d", score);

        glPushMatrix();
        glTranslated(200.0, 620.0, 0.0);
        glScaled(0.4, 0.4, 1.0);
        
        // ★修正: ここも同様に囲む
        glPushMatrix(); 
        glLineWidth(5.0);
        glColor3d(0.0, 0.0, 0.0);
        for(char* p = scoreText; *p; p++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
        glPopMatrix(); 

        // 白文字
        glLineWidth(3.0);
        glTranslated(0.0, 0.0, 0.0);
        glColor3d(1.0, 1.0, 0.0); 
        for(char* p = scoreText; *p; p++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
        glPopMatrix();


        // -------------------------------------------------
        // C. 下中央: HPゲージ
        // -------------------------------------------------
        float barWidth = 800.0;  
        float barHeight = 40.0; 
        float barY = 50.0;       
        float barLeft = -barWidth / 2.0;
        float barRight = barWidth / 2.0;

        // "HP" 文字
        glPushMatrix();
        glTranslated(barLeft, barY + 50.0, 0.0);
        glScaled(0.3, 0.3, 1.0);
        
        char hpLabel[20];
        sprintf(hpLabel, "HP: %d", playerHP);
        
        // ★修正: ここも囲む
        glPushMatrix();
        glLineWidth(5.0);
        glColor3d(0.0, 0.0, 0.0); 
        for(char* p = hpLabel; *p; p++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
        glPopMatrix();

        // 白文字
        glLineWidth(3.0);
        glTranslated(0.0, 0.0, 1.0);
        glColor3d(1.0, 1.0, 1.0); 
        for(char* p = hpLabel; *p; p++) glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
        glPopMatrix();
        
        // ゲージ背景
        glColor4d(0.2, 0.2, 0.2, 0.8);
        glBegin(GL_QUADS);
            glVertex2d(barLeft - 5, barY - 5);
            glVertex2d(barRight + 5, barY - 5);
            glVertex2d(barRight + 5, barY + barHeight + 5);
            glVertex2d(barLeft - 5, barY + barHeight + 5);
        glEnd();

        // ゲージ本体
        if (playerHP <= 30) glColor4d(1.0, 0.0, 0.0, 1.0); // 赤
        else glColor4d(0.0, 1.0, 0.0, 1.0); // 緑

        float currentWidth = (float)playerHP / 100.0 * barWidth;
        if (currentWidth < 0) currentWidth = 0;
        if (currentWidth > barWidth) currentWidth = barWidth;

        glBegin(GL_QUADS);
            glVertex2d(barLeft, barY);
            glVertex2d(barLeft + currentWidth, barY);
            glVertex2d(barLeft + currentWidth, barY + barHeight);
            glVertex2d(barLeft, barY + barHeight);
        glEnd();
        
        glLineWidth(1.0); 
    }

    // ==========================================
    // リザルト画面
    // ==========================================
    else if (gameState == STATE_RESULT) {
        if (isGameOver) {
            // スコアを金色画像で表示（中央・少し大きく）
            drawNumber(score, 0.0, 255.0, 130.0);
        } else {
            // ランクは左のRANK枠、スコアは右のSCORE枠の中央に合わせる
            drawRank(finalRankGrade, -253.0, 315.0, 140.0); // 枠に収まるよう小さく＆少し上に
            drawNumber(score, 256.0, 306.0, 80.0);
        }
    }
    


    // 【新機能】ボス戦ヘルプ: WAVE3でプレイヤーが床の右下スポットに来たら、壁にボス攻撃パターン(rule2)を大きく表示
    if (gameState == STATE_PLAY && currentWave == 3 && footNum > 0 &&
        footPos[0][0] > 320.0 && footPos[0][1] < 180.0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texRule2);
        glColor4d(1.0, 1.0, 1.0, 1.0);
        glBegin(GL_QUADS);
            glTexCoord2f(0.0, 0.0); glVertex2d(-550.0, 700.0);
            glTexCoord2f(0.0, 1.0); glVertex2d(-550.0, 0.0);
            glTexCoord2f(1.0, 1.0); glVertex2d(550.0, 0.0);
            glTexCoord2f(1.0, 0.0); glVertex2d(550.0, 700.0);
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }

    glDisable(GL_BLEND);
    glFlush();
}



// ===== timer() から切り出した更新関数群（機能はそのまま・役割ごとに分割）=====

// アイテム取得判定（プレイヤーが触れたら効果発動）
void updateItemPickup() {
    if (footNum <= 0) return;
    for (int i = 0; i < MAX_ITEMS; i++) {
        if (!items[i].active) continue;
        double dx = footPos[0][0] - items[i].x;
        double dy = footPos[0][1] - items[i].y;
        double dist = sqrt(dx*dx + dy*dy);
        if (dist < 50.0) {
            items[i].active = false;
            if (items[i].type == 0) {
                playerHP += 50;
                if (playerHP > 100) playerHP = 100;
                healEffectTimer = 15;
                alSourcePlay(soundData[SND_ITEM_HEAL]);
            } else {
                attackBuffTimer = 500;
                alSourcePlay(soundData[SND_ITEM_BUFF]);
            }
        }
    }
}

// 弾の処理（移動・画面外消去・横振り/縦振りの弾消し・防御反射・被弾・反射弾の敵ヒット）
void updateBullets() {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;

        // 移動処理
        bullets[i].x += bullets[i].vx;
        bullets[i].y += bullets[i].vy;

        // 画面外なら消去
        if (bullets[i].y < 0 || bullets[i].y > 825 || bullets[i].x < -550 || bullets[i].x > 550) {
            bullets[i].active = false;
            continue;
        }

        // --- 敵の弾 -> プレイヤーへの干渉 ---
        if (bullets[i].isEnemyBullet) {
            if (footNum > 0) {
                double dx_p = footPos[0][0] - bullets[i].x;
                double dy_p = footPos[0][1] - bullets[i].y;
                double dist_p = sqrt(dx_p*dx_p + dy_p*dy_p);

                // 横振り攻撃で半径280以内の弾を消す
                if (sweepEffectTimer > 0) {
                     if (dist_p < 280.0) {
                         bullets[i].active = false;
                         alSourcePlay(soundData[SND_DEFENSE]);
                         continue;
                     }
                }

                // 縦振り攻撃で前方の弾を消す
                if (swingDownEffectTimer > 0) {
                    float plAngRad = (playerAngle - 90.0) * M_PI / 180.0;
                    float lx = bullets[i].x - footPos[0][0];
                    float ly = bullets[i].y - footPos[0][1];
                    float rx = lx * cos(-plAngRad) - ly * sin(-plAngRad);
                    float ry = lx * sin(-plAngRad) + ly * cos(-plAngRad);
                    if (rx > -80.0 && rx < 80.0 && ry > 0.0 && ry < 600.0) {
                         bullets[i].active = false;
                         alSourcePlay(soundData[SND_DEFENSE]);
                         continue;
                    }
                }

                // 防御の向き判定（前方80度以内ならガード可能）
                bool angleGuard = false;
                if (isDefending) {
                    float vecX = bullets[i].x - footPos[0][0];
                    float vecY = bullets[i].y - footPos[0][1];
                    float bulletAngle = atan2(vecY, vecX) * 180.0 / M_PI;
                    float angleDiff = bulletAngle - playerAngle;
                    while (angleDiff <= -180) angleDiff += 360;
                    while (angleDiff > 180) angleDiff -= 360;
                    if (fabs(angleDiff) < 80.0) angleGuard = true;
                }

                float bulletSize = (bullets[i].type == 1) ? 70.0 : ((bullets[i].type == 2) ? 25.0 : 15.0);
                int damage = (bullets[i].type == 1) ? 30 : ((bullets[i].type == 2) ? 20 : 10);

                // A. 防御成功（反射）
                if (isDefending && angleGuard && dist_p < (85.0 + bulletSize)) {
                    bullets[i].isEnemyBullet = false;
                    bullets[i].vx *= -1.5;
                    bullets[i].vy *= -1.5;
                    alSourcePlay(soundData[SND_DEFENSE]);
                    reflectFlashTimer = 10;
                }
                // B. 被弾（防御失敗）
                else if (dist_p < (30.0 + bulletSize)) {
                    bullets[i].active = false;
                    playerHP -= damage;
                    alSourcePlay(soundData[SND_DMG_PL]);
                    playerDamageTimer = 20;
                    if (playerHP <= 0) {
                        gameState = STATE_RESULT;
                        isGameOver = true;
                        sceneTransitionTimer = 120;
                        finalRankGrade = 'D';
                        alSourcePlay(soundData[SND_OVER]);
                    }
                }
            }
        }
        // --- 反射弾 -> 敵への干渉 ---
        else {
            for(int k=0; k<MAX_ENEMIES; k++) {
                if(enemies[k].active) {
                    double dx_e = enemies[k].x - bullets[i].x;
                    double dy_e = enemies[k].y - bullets[i].y;
                    double dist_e = sqrt(dx_e*dx_e + dy_e*dy_e);
                    float radius = (enemies[k].type == 1) ? 150.0 : 40.0;
                    if (dist_e < radius + 20.0) {
                        bullets[i].active = false;
                        if (enemies[k].type == 1 && bossIsDefending) {
                            alSourcePlay(soundData[SND_DEFENSE]);
                        } else {
                            enemies[k].hp -= 50;
                            alSourcePlay(soundData[SND_DMG_EN]);
                            enemies[k].hitTimer = 5;
                        }
                        if (enemies[k].hp <= 0) {
                            enemies[k].active = false;
                            if (enemies[k].type == 1) score += 1000; else score += 100;
                            alSourcePlay(soundData[SND_DMG_EN]);
                            if (currentWave == 2) enemiesKilledInWave2++;
                            if (currentWave == 3) {
                                 enemiesKilledThisWave = 1;
                                 gameState = STATE_RESULT;
                                 isGameOver = false;
                                 sceneTransitionTimer = 120;
                                 finalRankScore = (score * playerHP) / 100;
                                 if (finalRankScore >= 1500) finalRankGrade = 'S';
                                 else if (finalRankScore >= 1000) finalRankGrade = 'A';
                                 else if (finalRankScore >= 500) finalRankGrade = 'B';
                                 else finalRankGrade = 'C';
                                 alSourcePlay(soundData[SND_CLEAR]);
                            }
                        }
                    }
                }
            }
        }
    }
}

// 敵の行動（ボスの溜め/回復/防御/近接衝撃波/移動/攻撃・プレイヤーの攻撃命中・撃破判定）
void updateEnemies() {
    bossIsDefending = false;
    float plAngRad = (playerAngle - 90.0) * M_PI / 180.0;

    for(int i=0; i<MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        if (enemies[i].hitTimer > 0) enemies[i].hitTimer--;

        // ボスの溜め処理
        if (enemies[i].type == 1 && enemies[i].isCharging) {
            enemies[i].chargeTimer--;
            // 溜め中は体力が回復していく。攻撃で阻止しないとどんどん回復する
            enemies[i].hp += 4;
            if (enemies[i].hp > enemies[i].maxHp) enemies[i].hp = enemies[i].maxHp;
            if (enemies[i].chargeTimer <= 0) {
                enemies[i].isCharging = false;
                enemies[i].attackTimer = 120;
                if (enemies[i].attackPattern == 1) {
                    alSourcePlay(soundData[SND_BOSS_BEAM]);
                    double dx = footPos[0][0] - enemies[i].x;
                    double dy = footPos[0][1] - enemies[i].y;
                    for (int k=0; k < MAX_BULLETS; k++) {
                        if (!bullets[k].active) {
                            bullets[k].active = true;
                            bullets[k].isEnemyBullet = true;
                            bullets[k].type = 1;
                            bullets[k].x = enemies[i].x; bullets[k].y = enemies[i].y;
                            double rad = atan2(dy, dx);
                            bullets[k].angle = rad;
                            bullets[k].vx = cos(rad) * 5.0;
                            bullets[k].vy = sin(rad) * 5.0;
                            break;
                        }
                    }
                    enemies[i].attackPattern = 2;
                }
                else if (enemies[i].attackPattern == 2) {
                    alSourcePlay(soundData[SND_BOSS_EXP]);
                    int way = 20;
                    for (int j=0; j < way; j++) {
                        for (int k=0; k < MAX_BULLETS; k++) {
                            if (!bullets[k].active) {
                                int randTex = (rand() % 3);
                                GLuint currentTex = texBossExp;
                                if (randTex == 1) currentTex = texBossExp2;
                                else if (randTex == 2) currentTex = texBossExp3;
                                bullets[k].textureID = currentTex;
                                bullets[k].active = true;
                                bullets[k].isEnemyBullet = true;
                                bullets[k].type = 2;
                                bullets[k].x = enemies[i].x; bullets[k].y = enemies[i].y;
                                double rad = (2.0 * M_PI * j) / way;
                                bullets[k].vx = cos(rad) * 3.5;
                                bullets[k].vy = sin(rad) * 3.5;
                                break;
                            }
                        }
                    }
                    enemies[i].attackPattern = 0;
                }
            }
        }

        // ボス防御
        if (enemies[i].type == 1) {
            if (bossDefenseTimer > 0) {
                bossDefenseTimer--;
                if (bossDefenseTimer > 100) bossIsDefending = true;
            } else {
                if (enemies[i].attackTimer <= 0 && enemies[i].isCharging == false)
                    if (rand() % 400 == 0) bossDefenseTimer = 200;
            }
        }
        if (enemies[i].attackTimer > 0) enemies[i].attackTimer--;

        // 近づきすぎペナルティ：近接ゾーンに居続けると衝撃波で反撃
        if (enemies[i].type == 1 && footNum > 0) {
            float ddx = footPos[0][0] - enemies[i].x;
            float ddy = footPos[0][1] - enemies[i].y;
            float distToP = sqrt(ddx*ddx + ddy*ddy);
            if (distToP < 260.0) {
                enemies[i].closeRangeTimer++;
                if (enemies[i].closeRangeTimer == 90) {
                    alSourcePlay(soundData[SND_BOSS_BEAM2]);
                }
                if (enemies[i].closeRangeTimer > 130) {
                    playerHP -= 25;
                    playerDamageTimer = 25;
                    bossShockTimer = 24; bossShockX = enemies[i].x; bossShockY = enemies[i].y; // 衝撃波の炎を発生
                    alSourcePlay(soundData[SND_BOSS_EXP]);
                    enemies[i].closeRangeTimer = 0;
                    if (playerHP <= 0) {
                        gameState = STATE_RESULT;
                        isGameOver = true;
                        sceneTransitionTimer = 120;
                        finalRankGrade = 'D';
                        alSourcePlay(soundData[SND_OVER]);
                    }
                }
            } else {
                enemies[i].closeRangeTimer = 0;
            }
        }

        // 移動（溜め中は動かない）
        if (enemies[i].type == 1 && footNum > 0 && !enemies[i].isCharging) {
            float dtx = enemies[i].targetX - enemies[i].x;
            float dty = enemies[i].targetY - enemies[i].y;
            float distToTarget = sqrt(dtx*dtx + dty*dty);
            if (distToTarget < 10.0) {
                enemies[i].targetX = (rand() % 1000) - 500.0;
                enemies[i].targetY = (rand() % 700) + 50.0;
            } else {
                enemies[i].x += (dtx/distToTarget) * 2.0;
                enemies[i].y += (dty/distToTarget) * 2.0;
            }
        }

        // 敵攻撃（溜め中は新たな攻撃をしない）
        if (enemies[i].attackTimer <= 0 && currentWave >= 2 && footNum > 0 && !enemies[i].isCharging) {
            double dx = footPos[0][0] - enemies[i].x;
            double dy = footPos[0][1] - enemies[i].y;
            double dist = sqrt(dx*dx + dy*dy);
            if (dist < 800.0) {
                if (enemies[i].type == 1) {
                    if (enemies[i].attackPattern == 0) {
                        enemies[i].attackTimer = 120;
                        alSourcePlay(soundData[SND_E_SHOOT]);
                        float bulletSpeed = 4.0;
                        for (int angle = -1; angle <= 1; angle++) {
                            for (int k=0; k < MAX_BULLETS; k++) {
                                if (!bullets[k].active) {
                                    bullets[k].active = true;
                                    bullets[k].isEnemyBullet = true;
                                    bullets[k].type = 0;
                                    bullets[k].x = enemies[i].x; bullets[k].y = enemies[i].y;
                                    double rad = atan2(dy, dx) + (angle * 0.2);
                                    bullets[k].vx = cos(rad) * bulletSpeed;
                                    bullets[k].vy = sin(rad) * bulletSpeed;
                                    break;
                                }
                            }
                        }
                        enemies[i].attackPattern = 1;
                    } else if (enemies[i].attackPattern == 1) {
                        enemies[i].isCharging = true;
                        enemies[i].chargeTimer = 100;
                        alSourcePlay(soundData[SND_BOSS_BEAM2]);
                    } else {
                        enemies[i].isCharging = true;
                        enemies[i].chargeTimer = 100;
                        alSourcePlay(soundData[SND_BOSS_EXP]);
                    }
                } else {
                    enemies[i].attackTimer = 240;
                    alSourcePlay(soundData[SND_E_SHOOT]);
                    float bulletSpeed = 3.0;
                    for (int k=0; k < MAX_BULLETS; k++) {
                        if (!bullets[k].active) {
                            bullets[k].active = true;
                            bullets[k].isEnemyBullet = true;
                            bullets[k].type = 0;
                            bullets[k].x = enemies[i].x; bullets[k].y = enemies[i].y;
                            bullets[k].vx = (dx/dist) * bulletSpeed;
                            bullets[k].vy = (dy/dist) * bulletSpeed;
                            break;
                        }
                    }
                }
            }
        }

        // プレイヤーからの攻撃判定
        if (footNum > 0) {
            float distToEnemy = sqrt(pow(footPos[0][0] - enemies[i].x, 2) + pow(footPos[0][1] - enemies[i].y, 2));
            float enemyRadius = (enemies[i].type == 1) ? 500.0 : 150.0;
            // 横振り
            if (sweepEffectTimer == 14) {
                if (distToEnemy < 150.0 + enemyRadius) {
                     if (enemies[i].type == 1 && bossIsDefending) alSourcePlay(soundData[SND_DEFENSE]);
                     else {
                         int dmg = 20;
                         if (attackBuffTimer > 0) dmg = (int)(dmg * 1.5);
                         enemies[i].hp -= dmg;
                         alSourcePlay(soundData[SND_DMG_EN]);
                         enemies[i].hitTimer = 5;
                         if (enemies[i].type == 1 && enemies[i].isCharging) {
                             enemies[i].isCharging = false;
                             enemies[i].chargeTimer = 0;
                             enemies[i].attackTimer = 60;
                             enemies[i].attackPattern = 0;
                         }
                     }
                }
            }
            // 縦振り
            if (swingDownEffectTimer == 9) {
                float lx = enemies[i].x - footPos[0][0];
                float ly = enemies[i].y - footPos[0][1];
                float rx = lx * cos(-plAngRad) - ly * sin(-plAngRad);
                float ry = lx * sin(-plAngRad) + ly * cos(-plAngRad);
                if (rx > (-50.0 - enemyRadius) && rx < (50.0 + enemyRadius) && ry > 0 && ry < 600) {
                     if (enemies[i].type == 1 && bossIsDefending) alSourcePlay(soundData[SND_DEFENSE]);
                     else {
                         int dmg = 80;
                         if (attackBuffTimer > 0) dmg = (int)(dmg * 1.5);
                         enemies[i].hp -= dmg;
                         alSourcePlay(soundData[SND_DMG_EN]);
                         enemies[i].hitTimer = 5;
                         if (enemies[i].type == 1 && enemies[i].isCharging) {
                             enemies[i].isCharging = false;
                             enemies[i].chargeTimer = 0;
                             enemies[i].attackTimer = 60;
                             enemies[i].attackPattern = 0;
                         }
                     }
                }
            }
            if (enemies[i].hp <= 0) {
                enemies[i].active = false;
                if (enemies[i].type == 1) score += 1000; else score += 100;
                alSourcePlay(soundData[SND_DMG_EN]);
                if (rand() % 10 == 0) {
                    int slot = -1;
                    for(int k=0; k<MAX_ITEMS; k++) if(!items[k].active) { slot = k; break; }
                    if (slot != -1) {
                        items[slot].active = true;
                        items[slot].type = rand() % 2;
                        items[slot].x = enemies[i].x; items[slot].y = enemies[i].y;
                    }
                }
                if (currentWave == 2) enemiesKilledInWave2++;
                if (currentWave == 3) {
                     enemiesKilledThisWave = 1;
                     gameState = STATE_RESULT;
                     isGameOver = false;
                     sceneTransitionTimer = 120;
                     finalRankScore = (score * playerHP) / 100;
                     if (finalRankScore >= 1500) finalRankGrade = 'S';
                     else if (finalRankScore >= 1000) finalRankGrade = 'A';
                     else if (finalRankScore >= 500) finalRankGrade = 'B';
                     else finalRankGrade = 'C';
                     alSourcePlay(soundData[SND_CLEAR]);
                }
            }
        }
    } // 敵ループ終了
}

// LiDAR/キーボードからプレイヤーの足位置を取得
void updateLiDARInput() {
    if (useKeyboardMode) {
        float spd = 8.0;
        if (kbLeft)  footPos[0][0] -= spd;
        if (kbRight) footPos[0][0] += spd;
        if (kbUp)    footPos[0][1] += spd;
        if (kbDown)  footPos[0][1] -= spd;
        if (footPos[0][0] < -540.0) footPos[0][0] = -540.0;
        if (footPos[0][0] >  540.0) footPos[0][0] =  540.0;
        if (footPos[0][1] <   10.0) footPos[0][1] =   10.0;
        if (footPos[0][1] >  690.0) footPos[0][1] =  690.0;
        footNum = 1;
    } else {
        footNum = 0;
        FILE *fp = fopen("../LiDAR/footpoint.txt", "r");
        if (fp != NULL) {
            if (fscanf(fp, "%d", &footNum) == 1) {
                int pointsToRead = std::min(footNum, MAXNUM);
                for (int i = 0; i < pointsToRead; i++) {
                    if (fscanf(fp, "%f,%f", &footPos[i][0], &footPos[i][1]) != 2) break;
                }
            }
            fclose(fp);
        }
    }
}

// 防御（立ち止まり）と向き（歩いた方向）の更新
void updatePlayerMotion() {
    if (defenseEffectTimer > 0) defenseEffectTimer--;
    if (sweepEffectTimer > 0) sweepEffectTimer--;
    if (swingDownEffectTimer > 0) swingDownEffectTimer--;

    double mdx = 0.0, mdy = 0.0, moveDist = 0.0;
    if (footNum > 0) {
        mdx = footPos[0][0] - prevFootPos[0];
        mdy = footPos[0][1] - prevFootPos[1];
        moveDist = sqrt(mdx*mdx + mdy*mdy);
        avgMove = avgMove * 0.8 + (float)moveDist * 0.2;
    }
    bool notAttacking = (sweepEffectTimer <= 0 && swingDownEffectTimer <= 0);

    // 防御: その場で立ち止まると発動（移動平均でノイズに強く）
    {
        static bool prevDef = false;
        if (footNum > 0 && avgMove < 2.5 && notAttacking) {
            isDefending = true;
            defenseEffectTimer = 2;
            if (!prevDef) alSourcePlay(soundData[SND_DEFENSE]);
        } else {
            isDefending = false;
        }
        prevDef = isDefending;
    }

    // 向き: 歩いた方向へ向く（防御中・攻撃中は固定）
    if (footNum > 0) {
        if (moveDist > 5.0 && !isDefending && notAttacking) {
            float moveAngle = atan2(mdy, mdx) * 180.0 / M_PI;
            float angleDiff = moveAngle - playerAngle;
            while (angleDiff <= -180) angleDiff += 360;
            while (angleDiff > 180) angleDiff -= 360;
            playerAngle += angleDiff * 0.2;
        }
        prevFootPos[0] = footPos[0][0];
        prevFootPos[1] = footPos[0][1];
    }
}

// WAVE管理（クリア演出/紙吹雪・敵の補充・WAVE進行）
void updateWaveManager() {
    int activeEnemyCount = 0;
    for(int i=0; i<MAX_ENEMIES; i++) if (enemies[i].active) activeEnemyCount++;

    if (waveClearTimer > 0) {
        waveClearTimer--;
        spawnConfetti();
        for (int i = 0; i < MAX_CONFETTI; i++) {
            if (confetti[i].active) {
                confetti[i].x += confetti[i].vx;
                confetti[i].y += confetti[i].vy;
                if (confetti[i].y < 0) confetti[i].active = false;
            }
        }
        if (waveClearTimer == 1) {
            if (currentWave == 1) {
                currentWave = 2;
                if (pcMode) { alertType = 1; alertTimer = (int)(2.5 * fr); alertActive = true; }  // 「攻撃封印」アラート
                for(int i=0; i<MAX_ENEMIES; i++) enemies[i].active = false;
                for(int i=0; i<MAX_BULLETS; i++) bullets[i].active = false;
                for(int i=0; i<MAX_ITEMS; i++) items[i].active = false;
                attackBuffTimer = 0; healEffectTimer = 0;
            }
            else if (currentWave == 2) {
                currentWave = 3;
                if (pcMode) { alertType = 2; alertTimer = (int)(3.0 * fr); alertActive = true; }  // 「攻撃解放」＋ボス登場
                enemiesKilledThisWave = 0;
                // WAVE2の残り敵・弾を消す（消し忘れるとボスが出ず、残り敵撃破でリザルトに飛んでしまうバグの修正）
                for(int i=0; i<MAX_ENEMIES; i++) enemies[i].active = false;
                for(int i=0; i<MAX_BULLETS; i++) bullets[i].active = false;
                for(int i=0; i<MAX_ITEMS; i++) items[i].active = false;
            }
            waveClearTimer = 0;
            initConfetti();
        }
    }
    else {
        // ヘルプ表示中／WAVE突入アラート中は 制限時間・敵補充・WAVE進行を止める（フリーズ）
        if (bossHelpActive || alertActive) {
            // 何もしない
        }
        else if (currentWave == 1) {
            if (gameTimer > 0) {
                gameTimer--;
                if (activeEnemyCount < 4) spawnEnemy(0);
            } else {
                if (waveClearTimer == 0) waveClearTimer = 250;
            }
        }
        else if (currentWave == 2) {
            // 「倒した数＋今いる数」が目標未満の時だけ補充（総数が目標を超えず、残数カウンタと画面の敵数が一致する）
            if (activeEnemyCount < 2 && (enemiesKilledInWave2 + activeEnemyCount) < enemiesTargetWave2) {
                spawnEnemy(0);
            }
            if (enemiesKilledInWave2 >= enemiesTargetWave2 && activeEnemyCount == 0) {
                if (waveClearTimer == 0) waveClearTimer = 250;
            }
        }
        else if (currentWave == 3) {
            if (activeEnemyCount == 0 && enemiesKilledThisWave == 0) {
                spawnEnemy(1);
            }
        }
    }
}

// メニュー画面（スタート/ルール/リザルト）で鎌を振ったら次へ遷移
void updateMenuTransition() {
    bool isSwing = (fabs(val[3]) > 300.0 || fabs(val[4]) > 300.0);
    if (isSwing && sceneTransitionTimer == 0) {
        if (gameState == STATE_START) {
            gameState = STATE_RULE;
            rulePage = 0; // ルールは1ページ目（遊び方）から
            sceneTransitionTimer = 250;
            alSourcePlay(soundData[SND_ATK_H]);
        }
        else if (gameState == STATE_RULE) {
            // ルールは「遊び方」1ページのみ。振ったらそのままゲーム開始。
            // （ボスの攻撃パターンはボス戦中の床ヘルプスポットで見せる）
            resetGame();
            gameState = STATE_PLAY;
            alSourcePlay(soundData[SND_ATK_V]);
        }
        else if (gameState == STATE_RESULT) {
            gameState = STATE_START;
            sceneTransitionTimer = 100;
            alSourcePlay(soundData[SND_ATK_H]);
        }
    }
}

// アイテムの定期生成（一定時間ごとにランダムな場所へ）
void updateItemSpawn() {
    itemSpawnTimer++;
    if (itemSpawnTimer > 1000) {
        itemSpawnTimer = 0;
        int slot = -1;
        for(int k=0; k<MAX_ITEMS; k++) if(!items[k].active) { slot = k; break; }
        if (slot != -1) {
            items[slot].active = true;
            items[slot].type = rand() % 2;
            items[slot].x = (rand() % 800) - 400.0;
            items[slot].y = (rand() % 400) + 100.0;
        }
    }
}

// M5Stickの振り入力 → 縦振り/横振りの攻撃判定（発動は triggerAttack に統合）
void updateAttackInput() {
    float atk_thresh = 300.0;
    float reset_threshold = 80.0;
    float dominance_ratio = 1.3;

    // 攻撃リセット判定（振り抜き後、値が小さくなったら次の攻撃を許可）
    if (!canAttack) {
        if (fabs(val[3]) < reset_threshold && fabs(val[4]) < reset_threshold) {
            canAttack = true;
        }
    }

    if (canAttack) {
        float abs_vert = fabs(val[3]); // 縦回転(Pitch)
        float abs_hori = fabs(val[4]); // 横回転(Roll)
        bool isHeldSideways = (fabs(val[0]) > 0.75); // 鎌が横に寝ているか
        // 縦振り
        if (val[3] > atk_thresh && abs_vert > abs_hori * dominance_ratio && !isHeldSideways) {
            triggerAttack(0);
        }
        // 横振り
        else if ( (abs_hori > atk_thresh && abs_hori > abs_vert * 0.8) ||
                  (val[3] > atk_thresh && isHeldSideways) ) {
            triggerAttack(1);
        }
    }
}

void timer(int value)
{
    // 次回のタイマーセット
    glutTimerFunc(1000/fr, timer, 0);

    gFrame++; // 演出アニメ用のフレームカウンタ（スポットの明滅などに使う）

    // --- 共通タイマーの減算 ---
    if (sceneTransitionTimer > 0) sceneTransitionTimer--;
    if (playerDamageTimer > 0) playerDamageTimer--;
    if (attackBuffTimer > 0) attackBuffTimer--;
    if (healEffectTimer > 0) healEffectTimer--;
    if (reflectFlashTimer > 0) reflectFlashTimer--;

    // センサー値取得
    getSerialData();

    // 
    // A. ゲームプレイ中「以外」の処理 (スタート、ルール、リザルト)
    // 
    if (gameState != STATE_PLAY) {
        // メニュー画面: 鎌を振ったら次へ
        updateMenuTransition();
    }
    //
    // B. ゲームプレイ中の処理（役割ごとの update 関数を順に呼ぶ）
    //
    else {
        updateItemSpawn();     // アイテムの定期生成
        updateAttackInput();   // M5Stickの振り入力 → 攻撃判定

        // LiDAR/キーボードから足位置を取得
        updateLiDARInput();

        // PC版: 右下スポットに乗ったら、そのWAVEの遊び方を全画面表示＆バトル一時停止。
        //       スペースで閉じて再開。離れて踏み直すと再表示（展示版は壁に出すので対象外）。
        //       クリア演出中・突入アラート中は発動しない。
        if (pcMode && waveClearTimer <= 0 && !alertActive) {
            bool onSpot = (footNum > 0 && footPos[0][0] > 320.0 && footPos[0][1] < 180.0);
            if (!onSpot) bossHelpArmed = true;
            if (onSpot && bossHelpArmed && !bossHelpActive) { bossHelpActive = true; bossHelpArmed = false; }
        } else {
            bossHelpActive = false;
        }

        // 防御（立ち止まり）と向き（歩いた方向）の更新
        updatePlayerMotion();

        // WAVE管理（敵の補充・WAVE進行・クリア演出）
        updateWaveManager();

        // WAVE突入アラートのカウントダウン（0になったら自動でバトル再開）
        if (alertTimer > 0) alertTimer--;
        if (bossShockTimer > 0) bossShockTimer--;

        // クリア演出中／ヘルプ表示中／突入アラート中は 敵・弾を止める
        if (waveClearTimer <= 0 && !bossHelpActive && !alertActive) {
            updateEnemies();    // 敵の行動（ボスAI・近接衝撃波・攻撃命中・撃破判定など）
            updateBullets();    // 弾の処理（移動・弾消し・反射・被弾）
            updateItemPickup(); // アイテム取得判定
        }
    } // gameState == STATE_PLAY end

    //
    // C. 強制再描画 (両ウィンドウ)
    //
    if (windowID_floor != 0) {
        glutSetWindow(windowID_floor);
        glutPostRedisplay();
    }
    if (windowID_wall != 0) {
        glutSetWindow(windowID_wall);
        glutPostRedisplay();
    }
}


void analyzeBuffer()
{
    char inData[6][BUFF_SIZE];
    int dataID = 0;
    int j = 0;
    
    for (int i=0; i<bufferPoint; i++) {
        inData[dataID][j] = bufferAll[i];
        
        if (inData[dataID][j]==',') {
            inData[dataID][j] = '\0';
            dataID++;
            if (dataID==6) break;
            j = 0;
        }
        else if (inData[dataID][j]=='\n') {
            inData[dataID][j] = '\0';
            dataID++;
            break;
        }
        else {
            j++;
        }
    }
    
    if (dataID==6) {
        for (int i=0; i<6; i++) {
            val[i] = atof(inData[i]);
        }
    }
}

void initGL()
{
    // --- ▼ 削除した部分: ウィンドウ生成とコールバック設定は main 関数へ移動 ---
    
    // 画面のクリア色設定
    glClearColor(0.1, 0.1, 0.3, 1.0); // 少し明るい紺色
// または
glClearColor(0.0, 0.0, 0.0, 1.0); // 黒（引き締まって見える場合もあります）

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_TEXTURE_2D); // テクスチャ有効化

    // --- 画像の読み込み (床側メイン画面用) ---
   

    texStageClear = loadTexture("stage_clear.png"); // ステージクリア演出の金色画像（床で使う）
    texClearFloor    = loadTexture("clear_floor.png");    // 床のクリア映像
    texGameOverFloor = loadTexture("gameover_floor.png"); // 床のゲームオーバー映像
    texBg[0]    = loadTexture("bg1.png");
    texBg[1]    = loadTexture("bg2.png");
    texBg[2]    = loadTexture("bg3.png");

    texNextStage = loadTexture("next_stage.png"); // 「NEXT STAGE」金色画像（床のクリア演出で表示）
    texHelpSpot  = loadTexture("help_spot.png");  // ボス戦ヘルプスポット（床の右下に表示）
    texHelpSpotPlain = loadTexture("help_spot_plain.png"); hasHelpPlain = assetExists("help_spot_plain.png"); // WAVE1の石マーク
    texHelpSpotGold  = loadTexture("help_spot_gold.png");  hasHelpGold  = assetExists("help_spot_gold.png");  // WAVE2の金コインマーク
    texWave1Help = loadTexture(helpVariant("wave1_help.png", "wave1_help_expo.png"));  // WAVE1ヘルプ（PC=キー/展示=動作名）
    texWave2Help = loadTexture(helpVariant("wave2_help.png", "wave2_help_expo.png"));  // WAVE2ヘルプ（同上）
    texWave2Notice = loadTexture("wave2_notice.png"); hasWave2Notice = assetExists("wave2_notice.png"); // WAVE2案内(日本語)
    texNokori = loadTexture("label_nokori.png"); hasNokori = assetExists("label_nokori.png");
    texTai    = loadTexture("label_tai.png");    hasTai    = assetExists("label_tai.png");
    arNotice = assetAspect("wave2_notice.png", 10.29);
    arNokori = assetAspect("label_nokori.png", 2.57);
    arTai    = assetAspect("label_tai.png",    1.33);
    texAlertSealed   = loadTexture("attack_sealed.png");   hasAlertSealed   = assetExists("attack_sealed.png");   // 「攻撃封印！」バナー
    texAlertUnsealed = loadTexture("attack_unsealed.png"); hasAlertUnsealed = assetExists("attack_unsealed.png"); // 「攻撃解放！」バナー
    alertSealedAR    = assetAspect("attack_sealed.png",   3.0);
    alertUnsealedAR  = assetAspect("attack_unsealed.png", 3.25);
    texWarning = loadTexture("warning.png"); hasWarning = assetExists("warning.png"); arWarning = assetAspect("warning.png", 3.5);
    texPressStart = loadTexture("press_start.png"); hasPressStart = assetExists("press_start.png"); arPressStart = assetAspect("press_start.png", 5.31);
    texTitleFloor = loadTexture("title_floor.png"); hasTitleFloor = assetExists("title_floor.png");
    texReturnBattle = loadTexture("return_battle.png"); hasReturnBattle = assetExists("return_battle.png"); arReturnBattle = assetAspect("return_battle.png", 7.61);
    texSkipPrompt = loadTexture("skip_prompt.png"); hasSkipPrompt = assetExists("skip_prompt.png"); arSkipPrompt = assetAspect("skip_prompt.png", 5.61);
    // カウントダウン数字を床コンテキストでも使えるよう読み込む（壁のtexNumとは別コンテキスト）
    for (int i = 0; i < 10; i++) {
        char p[64];
        snprintf(p, sizeof(p), "font/num_%d.png", i);
        texNumFloor[i] = loadTexture(p);
    }

    texBoss     = loadTexture("boss.png");
    
    texMob1     = loadTexture("mob1.png");
    texMob2     = loadTexture("mob2.png");
    texMob3     = loadTexture("mob3.png");
    texMob4     = loadTexture("mob4.png");

    texItemHeal = loadTexture("item_heal.png");
    texItemBuff = loadTexture("item_buff.png");

    texEffAtkH   = loadTexture("se_attack_h.png");
    texEffAtkV   = loadTexture("se_attack_v.png");
    texEffDef    = loadTexture("防御.png");
    texBullEnemy = loadTexture("se_enemy_shoot.png");
    texBullRef   = loadTexture("反射弾.png");
    texBossExpCharge = loadTexture("boss_exp.png");
    texBossExpFire = loadTexture("boss_exp2.png");
    texBossDefense = loadTexture("boss_df.png");
    texBossCharge = loadTexture("boss_beam.png");
    texBossDamaged = loadTexture("boss_damage.png");
    texBossBeamC = loadTexture("se_boss_beam1.png");
    texBossBeamF = loadTexture("se_boss_beam2.png");
    texBossExp = loadTexture("se_boss_exp.png");
    texBossExp2 = loadTexture("se_boss_exp2.png");
    texBossExp3 = loadTexture("se_boss_exp3.png");
    texBossShock = loadTexture("se_boss_shock.png"); // ボス近接衝撃波の炎

    // ★注意: 壁用の画像(texWallBg)はここではなく、main関数で壁ウィンドウを作った後に読み込みます
}
void reshape(int w, int h)
{
    winW = w; winH = h;   // PC版の小窓配置で使う
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-550.0, 550.0, 0.0, 700.0);
    glMatrixMode(GL_MODELVIEW);
}

void keyboard(unsigned char key, int x, int y)
{
    switch (key) {
        // --- デバッグ・強制進行用 ('z'キー) ---
        case 'z': 
            // 画面切り替え時に攻撃が出ないようにフラグをリセット
            swingDownEffectTimer = 0;
            sweepEffectTimer = 0;
            canAttack = true;
            isDefending = false;
            defenseEffectTimer = 0;

            // ▼ 状態ごとの遷移処理
            if (gameState == STATE_START) {
                // スタート -> ルール
                gameState = STATE_RULE;
                sceneTransitionTimer = 60;
                alSourcePlay(soundData[SND_ATK_H]);
            }
            else if (gameState == STATE_RULE) {
                // ルールは「遊び方」1ページのみ。zでそのままゲーム開始 (WAVE 1)
                resetGame();
                gameState = STATE_PLAY;
                alSourcePlay(soundData[SND_ATK_V]);
            }
            else if (gameState == STATE_PLAY) {
                sceneTransitionTimer = 0;
                if (currentWave == 3) {
                    // WAVE3: ボス撃破扱い -> クリア画面へ
                    for(int i=0; i<MAX_ENEMIES; i++) enemies[i].active = false;
                    for(int i=0; i<MAX_BULLETS; i++) bullets[i].active = false;
                    enemiesKilledThisWave = 1;
                    gameState = STATE_RESULT;
                    isGameOver = false;
                    finalRankGrade = 'S'; // デバッグ用の仮ランク
                    score += 5000;
                    sceneTransitionTimer = 120;
                    alSourcePlay(soundData[SND_CLEAR]);
                } else {
                    // WAVE1/2
                    if (waveClearTimer == 0) {
                        // まだ演出前：STAGE CLEAR! 演出を開始（updateWaveManagerが進行）
                        waveClearTimer = 250;
                    } else if (waveClearTimer > 2) {
                        // カウントダウン中にzを押したら、待たずに次ステージへスキップ。
                        // updateWaveManagerは waveClearTimer==1 でWAVE遷移するので、2にして次フレームで発火させる。
                        waveClearTimer = 2;
                    }
                }
            }
            else if (gameState == STATE_RESULT) {
                // リザルト -> スタート画面へ
                if (sceneTransitionTimer <= 0) {
                    gameState = STATE_START;
                    sceneTransitionTimer = 60;
                    alSourcePlay(soundData[SND_ATK_H]);
                }
            }
            break;

        // --- 終了 ---
        case 27: // ESCキー
            exit(0);
            break;

        // --- デバッグ用コマンド ---
        case ' ':  // スペース / Enter: 画面送り（PC版の決定キー）
        case 13:   // ※プレイ中は何もしない（誤爆防止。強制スキップはデバッグ用のzのまま）
            // ボス攻略ヘルプ表示中なら、閉じてバトル再開
            if (pcMode && bossHelpActive) { bossHelpActive = false; bossHelpArmed = false; canAttack = true; sweepEffectTimer = 0; swingDownEffectTimer = 0; break; }  // 閉じた瞬間に攻撃状態をリセット（ヘルプ後に攻撃できない不具合の保険）
            if (pcMode && alertActive) { alertActive = false; alertTimer = 0; break; }   // スペースでWAVE開始
            if (pcMode && gameState == STATE_PLAY && waveClearTimer > 2) { waveClearTimer = 2; break; }  // ステージクリア演出をスペースでスキップ
            if (gameState == STATE_PLAY) break;
            // 画面切り替え時に攻撃が出ないようにフラグをリセット
            swingDownEffectTimer = 0;
            sweepEffectTimer = 0;
            canAttack = true;
            isDefending = false;
            defenseEffectTimer = 0;
            if (gameState == STATE_START) {
                gameState = STATE_RULE;
                rulePage = 0;
                sceneTransitionTimer = 60;
                alSourcePlay(soundData[SND_ATK_H]);
            }
            else if (gameState == STATE_RULE) {
                resetGame();
                gameState = STATE_PLAY;
                alSourcePlay(soundData[SND_ATK_V]);
            }
            else if (gameState == STATE_RESULT && sceneTransitionTimer <= 0) {
                gameState = STATE_START;
                sceneTransitionTimer = 60;
                alSourcePlay(soundData[SND_ATK_H]);
            }
            break;

        case 'r': // ジャイロリセット
            playerAngle = 90.0;
            printf("Angle Reset!\n");
            break;

        case 'u': // 回復 (Heal)
            playerHP += 50;
            if (playerHP > 100) playerHP = 100;
            healEffectTimer = 15;
            alSourcePlay(soundData[SND_ITEM_HEAL]);
            printf("Debug: Healed!\n");
            break;

        case 'p': // パワーアップ (Power up)
            attackBuffTimer = 500;
            alSourcePlay(soundData[SND_ITEM_BUFF]);
            printf("Debug: Power Up!\n");
            break;

        // --- キーボード操作（M5Stick無しでテストするため）---
        case 'a': // 縦振り攻撃（オートエイム込みの共通処理）
            triggerAttack(0);
            break;
        case 's': // 横振り攻撃（オートエイム込みの共通処理）
            triggerAttack(1);
            break;
        case 'd': // デバッグ表示のオン/オフ
            debugMode = !debugMode;
            printf("Debug display: %s\n", debugMode ? "ON" : "OFF");
            break;

        case 'k': // 入力モード切替（キーボード ⇔ M5Stick）
            // Bluetoothポートが残っていてM5Stick無しでも繋がる場合などに手動で切替
            useKeyboardMode = !useKeyboardMode;
            if (useKeyboardMode) {
                // キーボードモードにしたら足の初期位置をセット
                footNum = 1; footPos[0][0] = 0.0; footPos[0][1] = 100.0;
            }
            printf("Input mode: %s\n", useKeyboardMode ? "KEYBOARD" : "M5STICK");
            break;

        default:
            break;
    }
}

// --- 矢印キー: キーボードモードでのプレイヤー移動 ---
// 押している間フラグを立て、timer()内で実際に座標を動かす
void specialKeyDown(int key, int x, int y) {
    switch (key) {
        case GLUT_KEY_UP:    kbUp = true; break;
        case GLUT_KEY_DOWN:  kbDown = true; break;
        case GLUT_KEY_LEFT:  kbLeft = true; break;
        case GLUT_KEY_RIGHT: kbRight = true; break;
    }
}
void specialKeyUp(int key, int x, int y) {
    switch (key) {
        case GLUT_KEY_UP:    kbUp = false; break;
        case GLUT_KEY_DOWN:  kbDown = false; break;
        case GLUT_KEY_LEFT:  kbLeft = false; break;
        case GLUT_KEY_RIGHT: kbRight = false; break;
    }
}

// シリアルポートからデータ取得 (軽量化版)
int getSerialData()
{
    char buffer[BUFF_SIZE];
    // 一度に読み込む量を制限するわけではないが、
    // read はノンブロッキングモード(O_NONBLOCK)なので、
    // データが来ていなければすぐに戻ってくるはず。
    
    int len = read(fd, buffer, BUFF_SIZE);
    if (len < 0) return 0; // エラーまたはデータなし
    
    // バッファがいっぱいになりそうなら、古いデータを捨てる (最新を優先)
    if (bufferPoint + len >= BUFF_SIZE) {
        bufferPoint = 0; // 強制リセット (多少のカクつきは許容してフリーズを防ぐ)
    }
    
    for (int i=0; i<len; i++) {
        bufferAll[bufferPoint] = buffer[i];
        bufferPoint++;
        
        if (buffer[i]=='\n') {
            analyzeBuffer();
            bufferPoint = 0; // 解析したらリセット
        }
    }
    
    return len;
}

// ▼▼▼ 音声初期化関数 (修正版) ▼▼▼
void initAL() {
    // WAV ファイル名
    const char* fileNames[SND_MAX] = {
        "se_attack_h.wav",      // 0: 横
        "se_attack_v.wav",      // 1: 縦
        "se_defense.wav",       // 2: 防御
        "se_enemy_shoot.wav",   // 3: 敵弾
        "大爆発1.wav",   // 4: 溜め音
        "se_boss_beam.wav",     // 4: ビーム
        "se_boss_exp.wav",      // 5: 爆発
        "se_dmg_player.wav",    // 6: プレイヤー被弾
        "se_dmg_player.wav",     // 7: 敵被弾 
        "se_clear.wav",         // 8: クリア
        "se_gameover.wav",      // 9: ゲームオーバー
        "se_item_heal.wav",     // 10: 回復
        "se_item_buff.wav"      // 11: 強化
    };

    ALuint buffer;
    
    // ★ ここからループ開始
    for (int i = 0; i < SND_MAX; i++) {
        // エラーリセット
        alGetError();

        // 音声は assets/ にまとめてあるので、パスを前置する
        char sndPath[256];
        snprintf(sndPath, sizeof(sndPath), "assets/%s", fileNames[i]);
        buffer = alutCreateBufferFromFile(sndPath);

        // 読み込みチェック
        if (buffer == AL_NONE) {
            printf("Error loading sound: %s\n", fileNames[i]);
            // エラー詳細を表示
            ALenum error = alutGetError();
            printf("ALUT Error: %s\n", alutGetErrorString(error));
        }

        // ソース生成と設定
        alGenSources(1, &soundData[i]);
        alSourcei(soundData[i], AL_BUFFER, buffer);
        alSourcei(soundData[i], AL_LOOPING, AL_FALSE);

        // 音量調整
        float vol = 1.0;
        if (i == SND_ATK_H || i == SND_ATK_V) vol = 2.0;
        if (i == SND_DMG_PL || i == SND_DMG_EN) vol = 2.0;
        if (i == SND_OVER || i == SND_CLEAR) vol = 1.5;

        alSourcef(soundData[i], AL_GAIN, vol);
        alSourcef(soundData[i], AL_PITCH, 1.0);
        
    } 
}
