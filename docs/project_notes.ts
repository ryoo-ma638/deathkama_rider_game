/**
 * Camagame_rider — 設計メモ & 課題トラッキング
 *
 * このファイルはTypeScriptとして書かれているが、実行はしない。
 * 型定義・設計の意図・課題の記録として使う。
 */

// =============================================================================
// センサーデータの型定義
// =============================================================================

/** M5StickC Plus2からシリアルで受け取る6軸データ */
type M5SensorData = {
  acc_x: number;  // val[0]: 左右傾き（G単位、-1.0〜1.0程度）
  acc_y: number;  // val[1]: 前後傾き（G単位）
  acc_z: number;  // val[2]: 重力方向（G単位、静止時≒1.0）
  gyro_x: number; // val[3]: Pitch角速度（°/s）→ 縦振り判定
  gyro_y: number; // val[4]: Roll角速度（°/s）  → 横振り判定
  gyro_z: number; // val[5]: Yaw角速度（°/s）   → プレイヤー向き回転
};

/**
 * 取り付け向きによる軸マッピングの注意
 *
 * M5StickC Plus2をLong辺が上（画面が正面）の場合:
 *   縦振り = gyro_x（val[3]）が大きく変化する
 *   横振り = gyro_y（val[4]）が大きく変化する
 *
 * M5StickC Plus2を横向き（USB口が右）で固定した場合:
 *   縦振り = gyro_y（val[4]）になる ← 逆転！
 *   横振り = gyro_x（val[3]）になる ← 逆転！
 *
 * → RULES.md の「軸確認の手順」で必ず確認すること
 */

// =============================================================================
// LiDARデータの型定義
// =============================================================================

/** LiDARが検出した1点の座標（ゲーム画面座標系） */
type FootPoint = {
  x: number; // -550 〜 550
  y: number; // 0 〜 700
};

/** footpoint.txt から読み込むデータ構造 */
type LiDARFrame = {
  count: number;       // 検出点数（0のとき足が検出されていない）
  points: FootPoint[]; // 各点の座標（最大1000点）
};

/**
 * LiDARの起動確認チェックリスト（ゲーム起動前に実行）:
 * 1. lidarapp を起動
 * 2. footpoint.txt が count > 0 になっていることを確認
 * 3. 足を動かして座標が変化することを確認
 */

// =============================================================================
// ゲームの状態管理
// =============================================================================

type GameState = "START" | "RULE" | "PLAY" | "RESULT";

type Wave = 1 | 2 | 3;

/** プレイヤーの状態 */
type PlayerState = {
  hp: number;           // 0〜100
  angle: number;        // プレイヤーの向き（度、90=上向き）
  x: number;            // LiDARから取得した足のX座標
  y: number;            // LiDARから取得した足のY座標
  isDefending: boolean; // 防御中フラグ
  attackBuffTimer: number; // 攻撃力UPの残り時間（フレーム）
};

/** 敵の種類 */
type EnemyType = "mob" | "boss";

/** 敵の状態 */
type EnemyState = {
  active: boolean;
  type: EnemyType;
  x: number;
  y: number;
  hp: number;
  maxHp: number;
  attackTimer: number;
  attackPattern: 0 | 1 | 2; // 0:通常, 1:ビーム溜め, 2:爆発溜め
  isCharging: boolean;
  chargeTimer: number;
  hitTimer: number; // ダメージ表示の残り時間
};

/** 弾の種類 */
type BulletType = 0 | 1 | 2; // 0:通常弾, 1:ビーム, 2:投石

type BulletState = {
  active: boolean;
  isEnemyBullet: boolean;
  x: number;
  y: number;
  vx: number;
  vy: number;
  type: BulletType;
};

// =============================================================================
// 既知バグ一覧
// =============================================================================

type BugPriority = "critical" | "high" | "medium" | "low";
type BugStatus = "open" | "partially_fixed" | "fixed" | "wontfix";

type Bug = {
  id: string;
  priority: BugPriority;
  status: BugStatus;
  file: string;
  line: string;
  description: string;
  fix: string;
};

const BUGS: Bug[] = [
  {
    // 【訂正2回】(1)当初「{}省略バグ」と記録→誤り。(2)次に「BがAを上書きする競合」と記録したが、
    // それは古いmain1画面.cppの話。最新main.cppではBのelseが削除済みで、その競合は解消されていた。
    id: "BUG-001",
    priority: "medium",
    status: "partially_fixed",
    file: "cgprog/main.cpp",
    line: "1553-1559(ブロックA) と 1587-1589(ブロックB)",
    description:
      "最新main.cppでは防御判定が2系統ある。" +
      "ブロックA: M5Stickの静止(isAlmostStill && isStickQuiet)で防御ON/OFF。" +
      "ブロックB: LiDARの静止(lidarStillTimer>30)で防御をONにする『バックアップ』。" +
      "main1画面と違いBにelseが無いので競合(上書き)は解消済み。" +
      "ただし2系統が混在していて分かりにくく、LiDARがノイズで揺れるとBが効かない点は残る。",
    fix:
      "【確定 2026-06-22】防御=その場で立ち止まる（LiDARの静止）に一本化する。" +
      "判定を1か所にまとめ、LiDARノイズに強くする（1フレーム差分でなく数フレーム平均で静止判定）。",
  },
  {
    id: "BUG-002",
    priority: "high",
    status: "open",
    file: "cgprog/main1画面.cpp",
    line: "全体",
    description:
      "timer() 関数が約1000行に膨れ上がり、入力・LiDAR・ゲームロジック・描画補助が混在。" +
      "バグの原因追跡が困難。",
    fix:
      "updateInput() / updateLiDAR() / updatePlayer() / updateEnemies() / " +
      "updateBullets() / updateWave() に分割する。",
  },
  {
    id: "BUG-003",
    priority: "high",
    status: "open",
    file: "cgprog/main1画面.cpp",
    line: "1780-1831",
    description:
      "initAL() 内で alGenBuffers() を呼んだ直後に alutCreateBufferFromFile() で" +
      "上書きしているため、生成したバッファが即リークする。",
    fix:
      "alGenBuffers() の戻り値を使わず、alutCreateBufferFromFile() の戻り値を" +
      "直接 buffer に代入するだけで良い（現状のコードは二度手間）。",
  },
  {
    id: "BUG-004",
    priority: "medium",
    status: "open",
    file: "cgprog/main1画面.cpp",
    line: "21",
    description:
      "シリアルポート名が特定のMac専用にハードコードされている。" +
      "別環境ではexit(1)で即終了する。",
    fix:
      "argc/argvでポート名を受け取るか、" +
      "接続失敗時はキーボード操作モードにフォールバックする。",
  },
  {
    id: "BUG-005",
    priority: "medium",
    status: "open",
    file: "cgprog/main1画面.cpp",
    line: "1105",
    description:
      "footpoint.txt を毎フレーム fopen/fclose している（50fps × ファイルIO）。" +
      "LiDAR側が書き込み中に読むと破損したデータを読む競合も起きうる。",
    fix:
      "当面はファイルIOを維持するが、読み込みをタイマーで間引く（5フレームに1回など）。" +
      "長期的にはUNIXソケットか共有メモリへ移行する。",
  },
  {
    id: "BUG-006",
    priority: "medium",
    status: "open",
    file: "cgprog/main1画面.cpp",
    line: "1609",
    description:
      "texBossDefense が2回 loadTexture() で上書きされている（同じファイル）。" +
      "1回目のテクスチャIDがリークする。",
    fix: "2行目の texBossDefense = loadTexture(\"boss_df.png\"); を削除する。",
  },
  {
    id: "BUG-007",
    priority: "low",
    status: "open",
    file: "cgprog/main1画面.cpp",
    line: "470-548",
    description:
      "スコア・ランクの描画に glutStrokeCharacter() を使用しているが、" +
      "座標オフセットが手動調整でありウィンドウリサイズに対応していない。" +
      "（新設計ではスコア表示は壁面ウィンドウに移る）",
    fix:
      "FreeTypeベースのフォント描画に切り替えるか、" +
      "数字スプライト画像(0-9)を用意してテクスチャ描画する。",
  },
  {
    // 「向きが定まらない」体感の主因。最新main.cppでは向きが3系統で制御されている。
    id: "BUG-008",
    priority: "high",
    status: "open",
    file: "cgprog/main.cpp",
    line: "1450-1452(タレット) / 1573-1578(歩行) / 1499-1517(オートエイム)",
    description:
      "最新main.cppではplayerAngleを3つの仕組みが書き換える。" +
      "(1)タレット旋回: ジャイロZで手動回転(1450)。(2)歩行: LiDAR移動方向に10%ずつ向く(1573)。" +
      "(3)オートエイム: 攻撃時に200以内の敵へスナップ(1515)。" +
      "(1)と(2)が同時に効くと引っ張り合って向きが定まらない。",
    fix:
      "【確定 2026-06-22】(1)タレット旋回は外す。(2)歩行=向きの主軸として残す。" +
      "(3)オートエイムは残す（普段は歩行方向、攻撃時だけ敵にスナップ）。" +
      "静止中は直前の向きを保持。これで向きは歩行＋攻撃時スナップの2系統に整理される。",
  },
  {
    id: "BUG-009",
    priority: "high",
    status: "open",
    file: "cgprog/main.cpp",
    line: "1567-1584",
    description:
      "向きを footPos[0] の1フレーム差分(dx,dy)から計算している（最新版も同じ）。" +
      "LiDARの点がノイズで揺れたり、どの点がindex0になるかが毎フレーム変わると、" +
      "差分がデタラメになり向きがランダムに回る。" +
      "また静止中は移動量がないため向きを更新できず、直前の値で固まる。",
    fix:
      "数フレーム分の移動を平均（移動平均）してから向きを計算しノイズを抑える。" +
      "将来的には足2点を検出して体の正面方向を直接求める（TASK-009）。",
  },
  {
    // 最新main.cppでは isHeldSideways(横持ち検知)で部分的に対処済み。ただし符号問題は残る。
    id: "BUG-010",
    priority: "medium",
    status: "partially_fixed",
    file: "cgprog/main.cpp",
    line: "1476, 1482, 1491-1492",
    description:
      "最新版では val[0]>0.75 で『鎌を横に寝かせている』を検知し(isHeldSideways)、" +
      "縦回転を横振りに振り分ける対処が入っている。" +
      "ただし縦振りは依然 val[3] > atk_thresh（正方向のみ）で、取り付け向き次第で" +
      "縦に振ったとき val[3] が負になると縦振りが発動しない。軸の符号確認が必要。",
    fix:
      "軸確認(TASK-004のデバッグ表示)で縦振りの符号を確認し、" +
      "必要なら fabs() に統一するか、正しい符号に合わせる。isHeldSidewaysの閾値も実機で調整。",
  },
  {
    id: "BUG-011",
    priority: "low",
    status: "open",
    file: "cgprog/main1画面.cpp",
    line: "1105",
    description:
      "footpoint.txt を相対パス '../LiDAR/footpoint.txt' で開いている。" +
      "ゲームを cgprog/ 以外のディレクトリから起動するとファイルが見つからず" +
      "プレイヤーが動かない。",
    fix:
      "起動スクリプトで作業ディレクトリを固定するか、" +
      "パスを引数か絶対パスで渡せるようにする。",
  },
];

// =============================================================================
// 今後の実装計画
// =============================================================================

type TaskStatus = "todo" | "in_progress" | "done";
type TaskPriority = "high" | "medium" | "low";

type Task = {
  id: string;
  priority: TaskPriority;
  status: TaskStatus;
  title: string;
  detail: string;
  relatedBugs?: string[]; // 関連するBug ID
};

const TASKS: Task[] = [
  {
    id: "TASK-001",
    priority: "high",
    status: "todo",
    title: "M5Stickスケッチの新規作成",
    detail:
      "arduino/m5stick_sender/m5stick_sender.ino を作成する。" +
      "送信フォーマット: acc_x,acc_y,acc_z,gyro_x,gyro_y,gyro_z\\n（50Hz）。" +
      "M5StickC Plus2の M5.Imu.getAccelData() / M5.Imu.getGyroData() を使用。",
    relatedBugs: ["BUG-004"],
  },
  {
    id: "TASK-002",
    priority: "high",
    status: "todo",
    title: "静止防御バグの修正",
    detail:
      "BUG-001 の修正。lidarStillTimer による防御判定を正しく実装し直す。" +
      "「X秒以上静止→防御状態に入る」「動き始めたら防御解除」の明確なステートマシンにする。",
    relatedBugs: ["BUG-001"],
  },
  {
    id: "TASK-003",
    priority: "high",
    status: "todo",
    title: "timer()関数の分割",
    detail:
      "updateInput() / updateLiDAR() / updatePlayer() / updateEnemies() / " +
      "updateBullets() / updateWave() に分割する。RULES.md の関数役割表を参照。",
    relatedBugs: ["BUG-002"],
  },
  {
    id: "TASK-004",
    priority: "high",
    status: "todo",
    title: "デバッグ表示モードの追加",
    detail:
      "d キーでオン/オフできるデバッグ表示を追加する。" +
      "表示内容: val[0]〜val[5]の値、footNum・足座標、playerAngle、lidarStillTimer、" +
      "isDefending フラグ、FPS。M5Stick軸確認に必須。",
  },
  {
    id: "TASK-005",
    priority: "medium",
    status: "todo",
    title: "2画面構想の再実装",
    detail:
      "【設計の前提】床面がメインのゲーム画面。敵・ボス・弾・攻撃エフェクト・プレイヤーは全部床に映る。" +
      "壁面はスコア・HP・WAVE数などのUIのみ。" +
      "プレイヤーは床を見ながら歩き回って戦うゲームなので、床がメインで当然。" +
      "旧コードの main(2画面用).cpp の drawFloorScene()/drawWallScene() を参考に、" +
      "glutSetWindow() で切り替えながら同一プロセスで両ウィンドウを管理する。" +
      "壁面が閉じられてもゲーム（床面）は続行できること。" +
      "【将来的な拡張】壁面にも敵を出して壁へ攻撃できるモード → 床が完成してから検討。",
  },
  {
    id: "TASK-006",
    priority: "medium",
    status: "todo",
    title: "フォント描画の改善",
    detail:
      "スコア・WAVEテキスト・HPの文字描画を改善する。" +
      "glutStrokeCharacter() の代わりに、数字・英字のスプライト画像を用意して" +
      "テクスチャ描画する方式（既存の画像読み込み基盤を流用できる）。",
    relatedBugs: ["BUG-007"],
  },
  {
    id: "TASK-007",
    priority: "medium",
    status: "todo",
    title: "シリアルポートのフォールバック",
    detail:
      "M5Stickが接続されていない場合、exit(1)で終了するのではなく " +
      "キーボードのみで操作できるモードに切り替える。" +
      "開発・デバッグ時にM5Stickなしで動作確認できるようにする。",
    relatedBugs: ["BUG-004"],
  },
  {
    id: "TASK-008",
    priority: "low",
    status: "todo",
    title: "ゲームバランス調整",
    detail:
      "RULES.md のゲームバランス基準値を実際にプレイしながら調整する。" +
      "特に WAVE2 の敵HP(80)とボスHP(2000)はプレイ感に大きく影響する。",
  },
  {
    id: "TASK-009",
    priority: "low",
    status: "todo",
    title: "足2点検出による体の向き精度向上",
    detail:
      "現在は footPos[0] の移動方向からプレイヤー角度を推定している。" +
      "LiDARで左右の足を2点検出できれば、足の角度から体の正面方向を直接計算できる。" +
      "footpoint.txt の複数点を活用する実装を検討する。",
  },
];

// =============================================================================
// ゲームルールの正式定義
// =============================================================================

/**
 * WAVEの設計意図（コードと口頭確認で確定）
 *
 * WAVE 1:
 *   目的: 30秒間でできるだけ多くの雑魚を倒してスコアを稼ぐ
 *   敵の弾: なし（currentWave >= 2 の条件でガードされている）
 *   意図: チュートリアル。縦振り・横振りの練習。
 *
 * WAVE 2:
 *   目的: 雑魚を合計4体倒す
 *   敵の弾: あり
 *   意図: 「防御で弾を跳ね返す」という操作を覚える。
 *
 * WAVE 3:
 *   目的: ボスを倒す
 *   敵の弾: あり（3パターンのローテーション）
 *   意図: 全操作（縦振り・横振り・反射）を組み合わせて戦う。
 */

/**
 * 将来の拡張アイデア：壁面の敵（空飛ぶ敵）
 *
 * コンセプト:
 *   壁に映る敵は「空中に浮いている」ため、鎌が直接届かない。
 *   床の敵の弾をガードで跳ね返すことで、壁面の敵を倒せる。
 *
 * ゲームデザイン上の意義:
 *   - 床の敵 → 直接攻撃で倒す
 *   - 壁の敵 → 反射でしか倒せない
 *   - 「防御」が純粋な受け身だけでなく攻撃手段になる
 *   - 2画面（床・壁）を使う意義が明確になる
 *
 * 実装タイミング: 床のゲームが完成してから着手。
 */

// =============================================================================
// 旧バージョン履歴メモ
// =============================================================================

type VersionNote = {
  file: string;
  mtime: string;       // 最終更新日時（どれが新しいかの客観的証拠）
  lines: number;
  description: string;
  status: "latest_base" | "reference_only" | "archived";
};

/**
 * 【整合性確認 2026-06-22】旧フォルダの cgprog には .cpp が8個あった。
 * 複製・実験を繰り返した結果ぐちゃぐちゃになっていた。
 * 更新日時を調べた客観的事実：
 *   - 最新・最終版は main.cpp（無印）。main(2画面用).cpp の9分後に保存された微修正版（差分55行）。
 *   - その4分後に a.out が生成 → 発表で動かした版は main.cpp。
 *   - 新プロジェクトのベースは main.cpp に確定し、Camagame_rider/cgprog/ に複製済み。
 *
 * 【重要な訂正】当初このメモやREADMEのバグ分析は、古い main1画面.cpp を読んで書いていた。
 * 最新 main.cpp を読み直したところ、指摘したバグの多くは既に対処されていた（下のBUG-001等参照）。
 */
const OLD_VERSION_NOTES: VersionNote[] = [
  {
    file: "授業版/cgprog/main.cpp",
    mtime: "2026-01-20 15:36",
    lines: 2333,
    description:
      "最新・最終版。2画面対応・オートエイム・横持ち対応(isHeldSideways)・" +
      "WAVEクリア演出(紙吹雪)などmain1画面より進化。a.out(発表で動いた版)の元。" +
      "→ 新プロジェクトのベース。",
    status: "latest_base",
  },
  {
    file: "授業版/cgprog/main(2画面用).cpp",
    mtime: "2026-01-20 15:27",
    lines: 2362,
    description:
      "main.cppの1つ前。差分はわずか55行（床専用画像を入れていたが、" +
      "main.cppで背景流用に戻した＋敵サイズ調整）。実質main.cppとほぼ同じ。",
    status: "reference_only",
  },
  {
    file: "授業版/cgprog/mainのコピー.cpp",
    mtime: "2025-12-24 12:57",
    lines: 1893,
    description: "1画面版の複製＋実験。2画面化の前段階。",
    status: "archived",
  },
  {
    file: "授業版/cgprog/main1画面.cpp",
    mtime: "2025-11-25 16:57",
    lines: 1831,
    description:
      "1画面で作り込んだ版。当初このメモが分析していたのはコレ（古い）。" +
      "防御の二重定義・縦振り符号などが未対処の状態。最新版では対処済み。",
    status: "reference_only",
  },
  {
    file: "授業版/cgprog/main(土台).cpp",
    mtime: "2025-11-20 15:26",
    lines: 781,
    description: "土台（初期の骨組み）。",
    status: "archived",
  },
  {
    file: "授業版/cgprog/main(初期lidar用).cpp",
    mtime: "2025-11-18 15:34",
    lines: 285,
    description: "最初期。LiDAR導入の試作。",
    status: "archived",
  },
  {
    file: "授業版/cgprog/main（画像クラッシュ）.cpp",
    mtime: "2025-11-18 14:55",
    lines: 890,
    description: "画像読み込みでクラッシュした版。",
    status: "archived",
  },
];

// =============================================================================
// 変数名メモ（旧コードから継承する際の対応表）
// =============================================================================

/**
 * 旧コードの主要グローバル変数と、新コードでの推奨名の対応
 *
 * 旧名                   → 新名（推奨）
 * val[3]                 → sensorData.gyro_x
 * val[4]                 → sensorData.gyro_y
 * val[5]                 → sensorData.gyro_z
 * sweepEffectTimer       → horizontalAttackTimer
 * swingDownEffectTimer   → verticalAttackTimer
 * lidarStillTimer        → standingTimer
 * currentWave            → wave（Waveの列挙型に）
 * enemiesKilledInWave2   → wave2KillCount（意味が明確に）
 */

// =============================================================================
// 実装進捗ログ（2026-06-25 時点）← 残作業の正本
// =============================================================================
/**
 * ■ 完了したこと
 *  [基盤・操作（〜2026-06-23）]
 *  - プロジェクト整理（研究室へ移動）・ドキュメント整備・ビルド環境確認(g++でビルド可)
 *  - キーボードモード（M5Stick/LiDAR無しでも動作）・デバッグ表示(dキー)・入力モード切替(kキー)
 *  - assets/ パス対応・シリアル接続失敗時のフォールバック
 *  - 向きの再設計：タレット旋回廃止、「歩行方向＋攻撃時オートエイム」の2系統に（BUG-008解消）
 *  - 防御の再設計：立ち止まり一本化＋移動平均(avgMove)でノイズ対策（BUG-001解消）
 *  - 攻撃を triggerAttack() に統合（M5Stick/キーボード共通・オートエイム込み）
 *  - フォント実装：リザルトのランク/スコアを金色スプライト画像に（drawNumber/drawRank・座標調整済）
 *  - バグ修正：画面揺れglPushMatrixのpop欠落（クリア文字ドリフト）／攻撃中の防御シールドワープ
 *  - ボス修正：ビーム当たり判定拡大(30→100)／溜め処理のcontinue削除（溜め中も攻撃が当たる）
 *  - ボス新機能：溜め中hp回復(+4)／攻撃で溜めキャンセル（阻止）／近接衝撃波(260以内2.6秒で25dmg)＋赤点滅予兆
 *  - 壁の常時UIはシンプル維持で確定（リザルトだけ金色＝メリハリ）
 *
 *  [構造・演出・画像（2026-06-23〜25 に追加で完了）]
 *  - timer()の関数分割（約700行→約40行・updateXxx 9関数に・機能不変）… BUG-002 解消
 *  - 2画面の役割整理（床=ゲーム本体／壁=UIのみ・床のSCORE/HP等を削除）… 解消
 *  - 起動スクリプト run.sh（作業ディレクトリ固定＋ビルド＋起動・引数lidarでLiDARも）… BUG-011 解消
 *  - texBossDefense の二重 loadTexture 解消（load は1回）… BUG-006 解消
 *  - WAVE通し完成：1→2→3→クリア/ゲームオーバー（WAVE2→3でボスが出ずリザルト直行したバグを修正）
 *  - STAGE CLEAR! 演出を金色画像化（床に表示）＋演出中は敵弾停止・壁UIを隠す
 *  - NEXT STAGE を金色画像＋金色カウントダウン数字に（床用に texNumFloor を別読込）。zでカウントダウンを即スキップ可
 *  - ルール1ページ化（遊び方のみ。ボス攻撃パターンはボス戦中の床ヘルプスポットで見せる）
 *  - ヘルプスポット（WAVE3・床の右下・踏むと光る＝壁にボス攻略 rule2 を大きく表示）
 *  - 床のリザルト映像を分割（クリア=clear_floor / ゲームオーバー=gameover_floor）
 *  - 画像不具合修正：透過保存でRGBになった画像が GL_UNPACK_ALIGNMENT(既定4)で崩れた（灰色の斜め走査線）
 *      → loadTexture に glPixelStorei(GL_UNPACK_ALIGNMENT,1) を追加（全画像を恒久保護）＋該当画像をRGBAで再保存
 *  - 素材画像のロゴ透かし除去（地面 bg1/bg2/bg3・clear_floor・壁 2壁/3壁）
 *  - リザルトのランク判定はスコアで S/A/B/C/D（被弾でD）を実装済みと確認（zの即クリアのみ仮'S'）
 *  - 床画像の高解像度化（clear_floor=2000px・gameover_floor=2400px・Lanczos拡大＋アンシャープ）
 *
 * ■ 残っている作業（2026-06-25）
 *  [今できる・プレイ確認]
 *    1. 直近の変更を通しプレイで確認（NEXT STAGE／ヘルプスポット／ルール1ページ／clear_floor金色／高解像度床／Zスキップ）
 *    2. バランス調整（ボスHP2000・近接衝撃波・溜め回復・各ダメージ・WAVE1制限30秒・敵数）… プレイしながら
 *    3. リザルト座標の最終微調整（必要なら）
 *  [M5Stick入手後・ハード（要実機）]
 *    4. M5Stickスケッチ書き込み＋軸実測（縦/横振り・静止）… BUG-010
 *    5. 軸確認後の攻撃しきい値の調整（現状ジャイロ>300）
 *    6. LiDAR実機での足追跡テスト（footpoint.txt連携・座標スケール合わせ）… BUG-005/009
 *    7. 2画面の物理設置（床プロジェクタ＋壁）での見え方・ヘルプスポット位置の最終調整
 *  [任意・将来]
 *    8. 床画像の「本物の」高解像度化（高解像度で作り直し）・title_floor/rule_floor も
 *    9. 振りの速さでダメージ可変／コンボ／壁の敵を反射で倒す（GAME_DESIGN.md 14章）／足2点検出で向き精度
 *  [子ども大学日進（2026夏・将来検討）]
 *   10. 子ども向け転用は一旦保留（当面は鎌のまま進行）。実施時は軽量・安全なプロップ（光る杖/フォーム剣等）へ
 *       差し替え＋振りしきい値の再調整＋敵（ガーゴイル等）のマイルド化を検討
 */

export {};
