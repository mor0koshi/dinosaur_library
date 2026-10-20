# mor0koshi_library

STM32 (HAL) のロボット用ライブラリです。次の 4 つを、`init` を呼ぶだけで使えるようにします。

| 部品 | できること |
|---|---|
| **CAN** (`can_*`) | 好きな ID の送受信。受信の振り分け、送信待ちの列、断線の判定 |
| **SBUS** (`sbus_*`) | プロポの受信。スティック (±1000)・スイッチの変換、断線とフェイルセーフの判定 |
| **エンコーダー** (`encoder_*`) | タイマーのエンコーダーモードで累積カウント・rpm |
| **ロボマス** (`robomas_*`) | M3508 (C620) / M2006 (C610) の電流・速度・位置制御 |

おまけで、エンコーダーの値を CAN で別の基板に送る `encoder_can_*` もあります。

- 対象: bxCAN を持つ STM32 F4 / F7 (F446・F767 でビルドを確認。実機での確認はこれから)。G4 / H7 の FDCAN には対応していません
- 言語: C11。malloc は使いません

## プロジェクトへの組み込み

このフォルダを CubeMX のプロジェクトの隣に置き (`STM32 project/mor0koshi_library`)、
プロジェクトの `CMakeLists.txt` に 2 か所足します。

```cmake
# 「Add STM32CubeMX generated sources」の add_subdirectory(cmake/stm32cubemx) の下に
add_subdirectory(../mor0koshi_library mor0koshi_library)

# 「Add linked libraries」に足す
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    mor0koshi_library
)
```

使うファイルで `#include "mor0koshi_library.h"` すれば全部使えます。

- CubeMX で有効にしていない機能 (CAN など) の部品は、自動でビルドから外れます
- 呼んだ部品だけがリンクされます (`sbus_init` を呼ばなければ SBUS の部分は入りません)

## 使い方

### CAN

CubeMX: CAN を有効にして通信速度を決め、**NVIC で RX0 と TX の割り込みをオン**にします。
`HAL_CAN_Start` とフィルタの設定は自分で書きません (`can_init` がやります)。
`can_init` は **1 つの CAN につき 1 回だけ**、`can_listen`・`can_subscribe`・`robomas_init` より**先に**呼びます
(後から呼ぶと登録が消えます)。別の `can_t` で同じ CAN を初期化しようとすると `LIB_ERR_PARAM` になります。

```c
can_t can1;
can_init(&can1, &hcan1);

// 送る (標準 ID、0〜8 バイト)
uint8_t d[8];
can_put_i16(d, 0, 1500);      // d[0..1] に int16 (リトルエンディアン)
can_put_float(d, 2, 3.14f);   // d[2..5] に float
can_send(&can1, 0x123, d, 8);

// 受け取る (最新の 1 フレームを取っておく箱)
can_box_t box;
can_listen(&can1, 0x456, &box);
if (can_read(&box, d)) {      // 新しく届いていれば true
    int16_t v = can_get_i16(d, 0);
}
if (can_age_ms(&box) > 100) { /* 100ms 届いていない = 断線 */ }

// 届いた瞬間に関数を呼ぶ (割り込みの中で動くので短く書く)
void on_rx(uint32_t id, const uint8_t *data, uint8_t len, void *arg) { ... }
can_subscribe(&can1, 0x456, on_rx, NULL);
```

- 送信のメールボックスは 3 つしかないので、ライブラリは 16 フレームの待ち列を持っています。
  列があふれると `can_send` が `LIB_ERR_FULL` を返します (ケーブルが抜けているときなど)
- 登録 (`can_listen`・`can_subscribe`) は 1 本の CAN につき 16 個までです
- ケーブルが抜けてバスオフになっても、自動で復帰します (ABOM)。送信の自動再送 (NART オフ) もオンにします

### SBUS

CubeMX: UART を **100000bps・9bit (Even パリティ)・ストップビット 2** にし、**RX の DMA (Normal。Circular は不可)** と
**UART の割り込み**をオンにします。F7 は Advanced Features の **RX Pin Active Level Inversion** もオンにします。
F4 の UART には反転の機能がないので、基板にインバーターが要ります。

```c
sbus_t sbus;
sbus_init(&sbus, &huart5);     // 1 つの UART につき 1 回だけ (別の sbus_t で同じ UART を初期化すると LIB_ERR_PARAM)

// メインループで毎周期 1 回
sbus_update(&sbus);
if (!sbus_ok(&sbus)) { /* 全部止める */ }
int ly  = sbus_stick(&sbus, 1);     // -1000〜+1000 (中立付近は 0)
int sw3 = sbus_switch3(&sbus, 6);   // -1 / 0 / +1 (生の値が大きいほうが +1)
int sw2 = sbus_switch2(&sbus, 4);   // 0 / 1
int raw = sbus_raw(&sbus, 0);       // 生の値 (0〜2047)
```

`sbus_ok` が false になるのは、1 フレームも来ていない・200ms 以上来ていない (コネクタ抜け)・
フェイルセーフ (送信機の電源 OFF) のときです。このときスティックとスイッチは 0 を返します。

> ⚠ **スイッチの押し直し**: 「スイッチを OFF にしたら解除」のような判定は、`sbus_ok` が true のときだけ
> OFF とみなしてください。NG の間はスイッチが 0 (OFF) と読めるので、それを OFF と扱うと
> ON のままでも解除されてしまいます。

送信機によって値の範囲が違うときは調整します (初期値は 2026_B_767 の値)。

```c
sbus_set_stick_range(&sbus, 368, 1024, 1680);  // 最小・中立・最大
sbus_set_deadband(&sbus, 26);                  // 中立から ±26 以内は 0
```

スイッチの境目は、この範囲の中間点 (中立と端の真ん中) から自動で決まります。

### エンコーダー

CubeMX: タイマーを **Encoder Mode (TI1 and TI2)**、Prescaler 0、Period を最大
(16bit なら 65535、TIM2 / TIM5 なら 4294967295) にします。割り込みは要りません。

```c
encoder_t enc1;
encoder_init(&enc1, &htim1, 2048);   // 2048 = 1 回転のカウント数 (512 パルス × 4 逓倍)
encoder_set_reverse(&enc1, true);    // 向きを逆にしたいとき

// 決まった周期 (1〜10ms) で呼ぶ
encoder_update(&enc1);
int32_t c = encoder_count(&enc1);    // 起動からの累積カウント
int32_t d = encoder_delta(&enc1);    // 前回の update からの変化量
float rpm = encoder_rpm(&enc1);      // 回転数 (符号付き)
float rev = encoder_turns(&enc1);    // 累積の回転数
encoder_reset(&enc1);                // 累積を 0 に戻す
```

- rpm は `update` を呼んだ間隔を CPU のサイクルカウンタで測って計算します。周期を指定する必要はありません
- 遅い回転を 1ms 周期で測ると粗くなります (2048 カウントなら 1 カウント ≒ 29rpm)。10ms 周期くらいがおすすめです

### エンコーダーの値を CAN で送る

エンコーダー 1 個を 1 フレーム (`[0..3]` int32 累積カウント、`[4..7]` float rpm) で送ります。

```c
// 送る側 (10ms ごと)
encoder_can_send(&can1, 0x300, &enc1);

// 受ける側
encoder_can_rx_t roller1;
encoder_can_listen(&can1, 0x300, &roller1);
float rpm = encoder_can_rpm(&roller1);
int32_t c = encoder_can_count(&roller1);
if (encoder_can_age_ms(&roller1) > 100) { /* 断線 */ }
```

### ロボマス (M3508 / M2006)

CAN を 1Mbps にして `can_init` します。ESC の ID は本体のボタンで 1〜8 に設定しておきます。

```c
robomas_t rm;
robomas_init(&rm, &can1);
robomas_motor_t *wheel = robomas_add(&rm, ROBOMAS_M3508, 1);
robomas_motor_t *arm   = robomas_add(&rm, ROBOMAS_M2006, 5);

// 1ms ごとの繰り返し (examples/robomas_sbus.c と同じ形)
sbus_update(&sbus);
if (!sbus_ok(&sbus)) {
    robomas_stop_all(&rm);                                   // プロポが切れたら全部止める
} else {
    robomas_set_rpm(wheel, (float)sbus_stick(&sbus, 1) * 0.3f);   // 速度制御: 出力軸 rpm
    robomas_set_angle(arm, (float)sbus_switch3(&sbus, 6) * 90.0f); // 位置制御: 出力軸の角度 [度]
}
robomas_update(&rm);   // 止めているときも呼び続ける (電流 0 を送り続ける)
```

使える指令 (最後に呼んだものが有効です。`set_*` は NG のときに呼ばないでください。`stop_all` を上書きしてしまいます):

```c
robomas_set_rpm(wheel, 300.0f);   // 速度制御: 出力軸 rpm
robomas_set_angle(arm, 90.0f);    // 位置制御: 出力軸の角度 [度] (720 なら 2 回転)
robomas_set_current(wheel, 2000); // 電流を直接 (M3508: ±16384 / M2006: ±10000)
```

読む値:

```c
float rpm = robomas_rpm(wheel);   // 出力軸 rpm
float deg = robomas_angle(arm);   // 出力軸の累積角度 (起動時の位置が 0 度)
robomas_reset_angle(arm);         // 今の位置を 0 度に
bool ok = robomas_ok(wheel);      // フィードバックが届いているか
```

- `robomas_update` はメインループからでも、タイマー割り込みの中からでも呼べます。メインループの
  `robomas_set_*` / `robomas_stop_all` と同時に動いても大丈夫です (指令の書き込みと計算は、割り込みを止めて行います)
- 値は全部**出力軸** (ギアの先) です。ギア比は種類から自動で入ります (M3508 ≒ 19.2、M2006 = 36)
- フィードバックが 100ms 届かないモーターは、自動で出力 0 になります
- CAN の断線や ESC の電源断のあいだに、ローターが回ると、累積角度は 1 回の途絶につき最大でローター半周ぶんずれることがあります。
  そうなったあとは `robomas_reset_angle` で原点を取り直してください

## HAL のコールバックについて

ライブラリは次の HAL のコールバックを自分で持っています。**同じ関数を自分で書くとリンクエラー
(multiple definition) になります。**

| 部品を使うと | ライブラリが持つコールバック |
|---|---|
| CAN の部品 (`can_*`・`robomas_*`・`encoder_can_*` のどれか) を使う | `HAL_CAN_RxFifo0MsgPendingCallback`、`HAL_CAN_TxMailbox0/1/2CompleteCallback`、`HAL_CAN_TxMailbox0/1/2AbortCallback` |
| SBUS (`sbus_*`) を使う | `HAL_UARTEx_RxEventCallback`、`HAL_UART_ErrorCallback` |

SBUS 以外の UART (Lidar など) の受信やエラーを扱いたいときは、`HAL_UARTEx_RxEventCallback` /
`HAL_UART_ErrorCallback` の代わりに、次の関数を書いてください。ライブラリが使わない UART の分はここに来ます。

```c
void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size) { ... }
void uart_error_hook(UART_HandleTypeDef *huart) { ... }
```

CAN の FIFO1 と `HAL_CAN_ErrorCallback` はライブラリが使わないので、自由に書けます。

## 名前がかぶったとき

関数名に頭の印を付けていないので、自分のコードに同じ名前 (`can_send`、`encoder_init` など) があると
リンクエラー (multiple definition) になります。どちらかの名前を変えてください。

## エラーの調べ方

`init`・`send` などは `lib_status_t` を返します。

| 値 | 意味 |
|---|---|
| `LIB_OK` | 成功 |
| `LIB_ERR_PARAM` | 引数がおかしい (ID が範囲外、同じ ID を二重に登録、NULL など) |
| `LIB_ERR_FULL` | 登録の表か送信待ちの列がいっぱい |
| `LIB_ERR_HAL` | HAL の関数が失敗した (`HAL_CAN_Start` を自分でも呼んでいる、など) |
| `LIB_ERR_IRQ_OFF` | CubeMX で必要な割り込みや DMA がオフ |

失敗するたびに `lib_error_hook` が呼ばれるので、書いておくと原因がすぐ分かります。

```c
void lib_error_hook(lib_status_t err, const char *where) {
    printf("lib error %d in %s\n", err, where);
}
```

CAN が抜けているときは 1ms ごとに呼ばれることがあるので、重い処理は書かないでください。
`lib_error_hook` は**割り込みの中から呼ばれることもあります** (SBUS のコールバックや、割り込みの中の `can_send`)。
上の `printf` の例を使うなら、`#include <stdio.h>` と、UART に出すための `_write` の書き換え (retarget) が要ります。
割り込みの中で長い表示をするとほかの処理が遅れるので、普段は表示せず、調べるときだけ書くのがおすすめです。

「つながっているのに動かない」ときは、`can_dropped_count` (送信で捨てた数)、`sbus_frame_count`
(受信したフレームの数)、`sbus_error_count` (UART エラーの数) を見ると切り分けられます。

## CAN の通信量の目安

1Mbps の CAN で 8 バイトのフレーム 1 つは約 0.13ms です。

- ロボマスは 1 台ごとに 1ms ごとにフィードバックを返すので、**1 台で約 13%** を使います。
  それに加えて、指令のフレーム (0x200、ID 5〜8 を使うなら 0x1FF も) を 1ms ごとに送ります。
  4 台なら フィードバック 4000 + 指令 1000 フレーム/秒で **約 65%**、5 台になると 0x200 と 0x1FF の 2 本が要るので **約 91%** です。
  **1 本の CAN にはおよそ 4 台まで**にし、それより多いときは CAN2 に分けてください
- エンコーダーを 6 個 1ms ごとに送ると約 8 割になります。**10ms ごと (約 8%)** がおすすめです
- 自分で決める ID は、ロボマス (0x1FF〜0x2FF) とぶつからない **0x300 番台**がおすすめです

## 実機での確認

新しい基板やプロジェクトで初めて使うときは、次の順に確かめてください。

- [ ] **SBUS**: `sbus_raw` の値を表示し、スティックを動かすと変わる
- [ ] **SBUS**: 受信機のコネクタを抜くと、200ms 以内に `sbus_ok` が false になる
- [ ] **SBUS**: 送信機の電源を切ると `sbus_ok` が false になる
- [ ] **CAN**: 2 枚の基板で `can_send` と `can_listen` をして、値が届く。片方の電源を切ると `can_age_ms` が増え続ける
- [ ] **エンコーダー**: 手で 1 回転させると `encoder_turns` がおよそ 1.0 (逆なら `encoder_set_reverse`)
- [ ] **ロボマス**: タイヤを浮かせて、`robomas_set_rpm` に小さい値 (10rpm くらい) を入れると、その速さで回る
- [ ] **ロボマス**: `robomas_set_angle(m, 360)` で出力軸が 1 回転して止まる
- [ ] **ロボマス**: プロポを切ると止まる (`robomas_stop_all`)

## ゲインの調整 (ロボマス)

初期値は次のとおりです。**M2006 の速度ゲイン以外は実機で確かめていない仮の値**なので、調整してください。

| | 速度 kp / ki / 積分上限 | 位置 kp / 上限 rpm |
|---|---|---|
| M3508 | 1.5 / 0.08 / 5000 (仮) | 2.0 / 100 (仮) |
| M2006 | 1.5 / 0.08 / 5000 (AIcontest で確認済み) | 2.0 / 100 (仮) |

```c
robomas_set_speed_gain(m, 1.5f, 0.08f, 5000.0f);  // kp, ki, 積分上限
robomas_set_angle_gain(m, 2.0f, 100.0f);          // kp (rpm / 度), 上限 rpm (出力軸)
```

1. 速度から合わせます。`robomas_set_rpm` で目標を出し、`robomas_rpm` を見ながら kp を上げます (振動したら下げる)
2. 目標に届かずに残る差は、ki を少しずつ上げて消します
3. 速度が合ったら位置です。`robomas_set_angle` で動かし、行き過ぎるなら位置の kp か上限 rpm を下げます

ゲインは「1ms ごとに `robomas_update`」が前提です。周期を変えると積分の効き方が変わります。

## 開発者向け

### PC でのテスト

HAL の偽物 (`test/fake/`) を使って、PC の gcc でライブラリをそのままテストします。

```bash
cmake -S test -B build-test -G Ninja && cmake --build build-test && ctest --test-dir build-test --output-on-failure
```

### ARM でのビルド確認

本物の CubeMX プロジェクトを一時フォルダにコピーして、その設定でビルドします (元のプロジェクトには触りません)。

```bash
test/arm_build_check.sh ../2026_B_767 compile
test/arm_build_check.sh ../2026_B_446 link test/linkcheck_f446.c \
    HAL_CAN_RxFifo0MsgPendingCallback HAL_CAN_TxMailbox0CompleteCallback \
    HAL_UARTEx_RxEventCallback HAL_UART_ErrorCallback
test/arm_build_check.sh ../mortor_test link test/linkcheck_nocan.c \
    HAL_UARTEx_RxEventCallback HAL_UART_ErrorCallback
```

`link` の最後に並べるのは、ライブラリのものになっているべき HAL のコールバックです。
それが無い (HAL の空の関数のまま) か、並べていないのにライブラリのものが入っていると NG になります。

設計の詳しいことは `docs/specs/2026-10-20-mor0koshi-library-design.md` にあります。
