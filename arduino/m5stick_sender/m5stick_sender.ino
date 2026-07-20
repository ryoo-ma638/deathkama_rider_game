/**
 * Camagame_rider — M5StickC センサー送信スケッチ
 *
 * 送信フォーマット（Serial, 115200bps）:
 *   acc_x,acc_y,acc_z,gyro_x,gyro_y,gyro_z\n
 *
 * 受信側(ゲーム)の val[] との対応:
 *   val[0]=accX, val[1]=accY, val[2]=accZ
 *   val[3]=gyroX, val[4]=gyroY, val[5]=gyroZ
 *
 * 取り付け向き: Long辺が上（画面が正面）を基準とする。
 *   gyro_x → 縦振り(Pitch) / gyro_y → 横振り(Roll) / gyro_z → 旋回(Yaw)
 *   ※実機で振って数値を見て確定すること（RULES.md の軸確認手順）
 *
 * ライブラリ: M5Unified（M5StickC対応・ボード自動判別）
 *   オブジェクトは M5、IMUは M5.Imu.getImuData() を使う
 */

#include <M5Unified.h>

// 送信間隔（ミリ秒）: ゲームが50fpsなので20msに合わせる
static const int SEND_INTERVAL_MS = 20;
unsigned long lastSendTime = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);

    M5.Display.setRotation(3);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.println("Camagame");
    M5.Display.println("Sender Ready");
}

void loop() {
    M5.update();      // ボタン等の更新
    M5.Imu.update();  // IMUの更新

    unsigned long now = millis();
    if (now - lastSendTime < SEND_INTERVAL_MS) {
        return;
    }
    lastSendTime = now;

    // 最新のIMUデータを取得
    auto data = M5.Imu.getImuData();

    // 送信（カンマ区切り6値 + 改行）
    Serial.print(data.accel.x, 3); Serial.print(",");
    Serial.print(data.accel.y, 3); Serial.print(",");
    Serial.print(data.accel.z, 3); Serial.print(",");
    Serial.print(data.gyro.x, 1);  Serial.print(",");
    Serial.print(data.gyro.y, 1);  Serial.print(",");
    Serial.println(data.gyro.z, 1);

    // 画面に数値を表示（軸確認用）
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("gX:%6.1f\n", data.gyro.x);  // 縦振りの候補
    M5.Display.printf("gY:%6.1f\n", data.gyro.y);  // 横振りの候補
    M5.Display.printf("gZ:%6.1f\n", data.gyro.z);  // 旋回の候補
    M5.Display.printf("aX:%5.2f\n", data.accel.x); // 横持ち判定の候補
}
