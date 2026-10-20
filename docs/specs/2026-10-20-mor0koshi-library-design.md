# mor0koshi_library 設計

- 日付: 2026-10-20
- 状態: 設計確定・実装前

## 目的

STM32 (HAL) のロボット用ファームウェアで、毎回書き直している次の 4 つを、`init` を呼ぶだけで使えるようにする。

1. **CAN 通信** … 好きな ID の送受信
2. **SBUS** … プロポの受信と、スティック・スイッチの値の変換
3. **エンコーダー** … タイマーのエンコーダーモードでの回転数・累積カウント
4. **ロボマスモーター** … M3508 (C620) / M2006 (C610) の速度制御・位置制御

元にしたコード:

| 部品 | 元 | 実機での確認 |
|---|---|---|
| SBUS | 2026_B_767 `sbus_handler.c` (デコード、断線・フェイルセーフの判定、スティック・スイッチの変換、UART エラーからの復帰) | 済 |
| CAN | 2026_B_767 `can_handler.c`、AIcontest `main.c` | 済 (受信のみ) |
| エンコーダー | 2026_B_446 `main.c` (TIM1〜8 のエンコーダーモード → 差分) | 済 |
| ロボマス | AIcontest `main.c` (0x200 送信、0x201〜 受信、M2006 の PI) | 済 (M2006 のみ) |

## 範囲

**やること**
- ライブラリ本体、README (日本語の使い方と実機確認の手順)、使う例 3 つ、PC で動くテスト
- F767 と F446 の実際の CubeMX 設定でビルドが通ることの確認

**やらないこと**
- 2026_B_767 / 2026_B_446 / AIcontest の置き換え (大会用で動いているので触らない。移すのは別の作業)
- CAN の拡張 ID (29bit)
- FDCAN (G4 / H7)。対象は bxCAN を持つ F4 / F7 (L4 も同じ HAL なのでおそらく動くが、確認はしない)
- GM6020
- 実機での動作確認 (使う人が README の手順で行う)

## 全体の構成

```
STM32 project/mor0koshi_library/   ← 独立した git リポジトリ
  CMakeLists.txt                   ← 静的ライブラリ mor0koshi_library を定義
  README.md
  include/
    mor0koshi_library.h            ← これ 1 つを include すれば全部使える
    mor0koshi_common.h             ← lib_status_t、lib_error_hook
    mor0koshi_can.h
    mor0koshi_sbus.h
    mor0koshi_encoder.h
    mor0koshi_encoder_can.h
    mor0koshi_robomas.h
  src/
    mor0koshi_port.h               ← HAL・時間・DWT への入口 (テストでは偽物に差し替える)
    mor0koshi_common.c
    mor0koshi_can.c                ← CAN の HAL コールバックもここ
    mor0koshi_sbus.c               ← UART の HAL コールバックもここ
    mor0koshi_encoder.c
    mor0koshi_encoder_can.c
    mor0koshi_robomas.c
  examples/
    robomas_sbus.c                 ← プロポで M3508 / M2006 を回す
    encoder_sender.c               ← エンコーダー → CAN で送る基板
    encoder_receiver.c             ← CAN でエンコーダー値を受け取る基板
  test/
    CMakeLists.txt                 ← PC の gcc でビルドして ctest で走らせる
    fake/                          ← HAL の偽物 (main.h、型、HAL_ 関数、時間、DWT)
    test_*.c
  docs/specs/                      ← この設計書
```

### 部品どうしの関係

```
単独で動く:   sbus      encoder      can
                          │           ├── robomas (CAN を使う。エンコーダーは使わない)
                          └─ 任意 ─────┴── encoder_can (両方を使うときだけの橋渡し)
```

- `encoder` は CAN と関係なく、その基板の中で値をそのまま使える
- `can` は汎用の送受信で、エンコーダーとは関係ない
- `encoder_can` は、エンコーダーの値を別の基板に送るときだけ使うおまけ

### 名前の付け方

- **関数と型の頭には何も付けない** (`can_send`、`sbus_stick`、`encoder_rpm`、`robomas_set_rpm`)。部品の名前がそのまま頭になる
  - 使う側が同じ名前の関数を書くとビルドエラー (multiple definition) になる。黙って変な動きはしないが、そのときはどちらかの名前を変える。README に書く
- **ファイル名には `mor0koshi_` を付ける**。CubeMX の「周辺機能ごとに .c/.h を分ける」設定で生成される `can.h`・`tim.h`・`usart.h` とかぶらないようにするため

### プロジェクトへの組み込み

各プロジェクトの `CMakeLists.txt` に 2 か所足すだけ。

```cmake
add_subdirectory(../mor0koshi_library mor0koshi_library)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    mor0koshi_library
)
```

- `mor0koshi_library` は **STATIC ライブラリ**で、`target_link_libraries(mor0koshi_library PUBLIC stm32cubemx)` によってそのプロジェクトの HAL のインクルードパスとマクロ (`STM32F767xx` など) でコンパイルされる。そのため F767 でも F446 でも同じソースが使える
- ライブラリの中から HAL を使うときは `#include "main.h"` を通す (CubeMX のプロジェクトには必ずある)
- **使わない部品はコンパイルされない**: 各 .c は `HAL_CAN_MODULE_ENABLED` / `HAL_UART_MODULE_ENABLED` / `HAL_TIM_MODULE_ENABLED` が無ければ中身が空になる。SBUS だけのプロジェクトでも使える

### 決まりごと

- **malloc は使わない**。`can_t` や `encoder_t` などは使う側がグローバル変数として用意する
- **割り込みの中ではデータを写すだけ**。計算は、メインループから決まった周期で呼ぶ `update` 関数で行う。例外はロボマスの角度の積算で、これは受信割り込みの中で行う (理由は「ロボマス」の節)
- **ライブラリの中では止めない**。`Error_Handler()` は呼ばない

## HAL の割り込みコールバックの扱い

HAL のコールバックは 1 つのプロジェクトに 1 つしか書けない。ライブラリが自分で書いて振り分ける。

CAN の部品 (`can_*`・`robomas_*`・`encoder_can_*` のどれか) を使うと CAN のコールバックがリンクされ、SBUS (`sbus_*`) を使うと UART のコールバックがリンクされる。

| コールバック | 置き場所 | 振り分け |
|---|---|---|
| `HAL_CAN_RxFifo0MsgPendingCallback` | `mor0koshi_can.c` | `hcan` で `can_t` を探し、ID で箱・関数に配る |
| `HAL_CAN_TxMailbox0/1/2CompleteCallback`、`HAL_CAN_TxMailbox0/1/2AbortCallback` | `mor0koshi_can.c` | 送信待ちの列から次を出す |
| `HAL_UARTEx_RxEventCallback` | `mor0koshi_sbus.c` | 登録した `sbus_t` ならデコード。それ以外は `uart_rx_event_hook(huart, size)` へ |
| `HAL_UART_ErrorCallback` | `mor0koshi_sbus.c` | 登録した `sbus_t` なら受信を再開。それ以外は `uart_error_hook(huart)` へ |

- `uart_rx_event_hook` / `uart_error_hook` は中身が空の weak 関数。ライブラリが使わない UART (Lidar など) があるときだけ、使う側が書く
- ライブラリは静的ライブラリなので、**呼んだ部品の .c だけがリンクされる**。`sbus_init` を呼ばないプロジェクトでは `mor0koshi_sbus.c` がリンクされず、自分で書いた `HAL_UART_ErrorCallback` がそのまま使える
- 使う部品と同じ HAL コールバックを使う側も書くと、リンクエラー (multiple definition) になって気づける。そのときは自分の処理を `uart_error_hook` などに移す
- CAN の FIFO1 と `HAL_CAN_ErrorCallback` はライブラリが使わないので、使う側が自由に書ける

## エラー処理

```c
typedef enum {
    LIB_OK = 0,
    LIB_ERR_PARAM,    // 引数がおかしい (ID が範囲外、同じ ID を二重に登録、NULL など)
    LIB_ERR_FULL,     // 登録の表、または送信待ちの列がいっぱい
    LIB_ERR_HAL,      // HAL の関数が失敗した
    LIB_ERR_IRQ_OFF,  // CubeMX で必要な割り込みや DMA がオフ
} lib_status_t;

void lib_error_hook(lib_status_t err, const char *where);  // weak。中身は空
```

- `init`・`listen`・`subscribe`・`send`・`add` は `lib_status_t` を返す (`robomas_add` だけはモーターへのポインタを返し、失敗すると NULL)
- 失敗したときは必ず `lib_error_hook` も呼ぶ。使う側が書けば `printf` などで原因が分かる。`where` は関数名。割り込みの中から呼ばれることもある (SBUS のコールバック、割り込みの中の `can_send`)。`printf` には `stdio.h` と UART への `_write` の書き換えが要る
- NULL を渡された関数は何もしない (値を返す関数は 0 を返す。ただし `*_age_ms` は UINT32_MAX = 一度も届いていない扱い)。HardFault にはならない
- 診断用の数: `can_dropped_count(&can)` (送信で捨てた数)、`sbus_frame_count(&sbus)` (受信フレーム数)、`sbus_error_count(&sbus)` (UART エラーの数)

## 1. CAN (`mor0koshi_can`)

### CubeMX 側の準備

CAN を有効にして通信速度を決め、NVIC で **RX0 割り込み**と **TX 割り込み**をオンにする。受信フィルタは書かない。

### 使い方

```c
can_t can1;
can_init(&can1, &hcan1);   // フィルタ設定・ABOM・開始・割り込み有効化まで全部やる

// 送る (標準 ID のみ。len は 0〜8)
uint8_t d[8];
can_put_i16(d, 0, 1500);        // d[0..1]
can_put_float(d, 2, 3.14f);     // d[2..5]
can_send(&can1, 0x123, d, 8);

// 受け取る (かんたん版: 最新の 1 フレームを取っておく箱)
can_box_t box;
can_listen(&can1, 0x456, &box);
if (can_read(&box, d)) {        // 前回の can_read から新しく届いていれば true。d に 8 バイト写す
    int16_t v = can_get_i16(d, 0);
}
can_peek(&box, d);               // 新しいかどうかに関係なく最新の値を写す。一度も届いていなければ false
uint32_t age = can_age_ms(&box);  // 最後に届いてからの ms。一度も届いていなければ UINT32_MAX

// 受け取る (上級版: 届いた瞬間に関数を呼ぶ。割り込みの中で動くので短く書く)
void my_handler(uint32_t id, const uint8_t *data, uint8_t len, void *arg);
can_subscribe(&can1, 0x456, my_handler, NULL);
can_subscribe_range(&can1, 0x201, 0x208, my_handler, NULL);
```

詰める・取り出す関数 (すべて**リトルエンディアン**、`offset` はバイト位置):
`can_put_u8/i8/u16/i16/u32/i32/float(uint8_t *d, int offset, 値)`、`can_get_u8/i8/u16/i16/u32/i32/float(const uint8_t *d, int offset)`

### 中でやること

- **フィルタ**: 全部受け取る設定 (マスク 0) で FIFO0 へ。フィルタの番号は CAN1・CAN3 が 0、CAN2 が 14 (SlaveStartFilterBank = 14)
- **ABOM** (バスオフからの自動復帰): `HAL_CAN_Start` の前、初期化モードのうちに `MCR` の `ABOM` ビットを立てる。CubeMX の設定に関係なく有効になる
- **自動再送**: 同じく `NART` ビットを下ろす。オンのままだと送信に失敗したとき完了の割り込みが来ず、送信の列が止まる
- **1 つの CAN に 1 つの `can_t`**: `can_init` は 1 つの CAN につき 1 回、`can_listen`・`can_subscribe`・`robomas_init` より先に呼ぶ (同じ `can_t` をもう一度 `can_init` すると登録が消える)。別の `can_t` で、すでに登録された CAN を初期化しようとすると `LIB_ERR_PARAM` を返す。SBUS の `sbus_init` も同じ (別の `sbus_t` で同じ UART は `LIB_ERR_PARAM`)
- **NULL**: `can_put_*` は `d` が NULL なら何もせず、`can_get_*` は 0 を返す
- **設定忘れの検出**: `can_init` は `NVIC_GetEnableIRQ` で RX0 と TX の割り込みが有効か確かめ、オフなら `LIB_ERR_IRQ_OFF` を返す
- **受信の振り分け**: 1 本の CAN につき登録 16 個まで (`can_listen`・`can_subscribe`・`can_subscribe_range` の合計)。ID が重なる登録は両方に配る。どれにも当たらない ID は捨てる
- **送信待ちの列**: メールボックスは 3 つしかないので、16 フレームのリングバッファを持つ。`can_send` は空いているメールボックスがあれば直接入れ、無ければ列に積む。送信完了・中止の割り込みで列から順に出す。列があふれたら新しいほうを捨てて `LIB_ERR_FULL` を返し、捨てた数を数える
  - 列とメールボックスの操作は、割り込みを止めた短い区間で行う
- **箱の読み出し**: `can_read` は割り込みを止めてから写す (途中で次のフレームが来て混ざらないように)
- CAN1・CAN2・CAN3 を同時に使える (`can_t` は 3 つまで登録)
- `can_t` 全体の最終受信時刻も持ち、`can_bus_age_ms(&can1)` で何か 1 つでも届いてからの時間が分かる

## 2. SBUS (`mor0koshi_sbus`)

### CubeMX 側の準備

UART を 100000bps・9bit (偶数パリティ込み)・ストップビット 2 にし、RX の DMA (**Normal。Circular は不可**) と UART の割り込みをオンにする。F7 は「RX 反転」をオンにする。F4 の UART には反転の機能がないので、基板にインバーターが要る。

### 使い方

```c
sbus_t sbus;
sbus_init(&sbus, &huart5);              // DMA 受信開始まで全部やる

// メインループで毎周期 1 回
sbus_update(&sbus);                     // 最新フレームを写し取り、正常かどうか判定する

if (!sbus_ok(&sbus)) { /* 全部止める */ }
int ly  = sbus_stick(&sbus, 1);         // -1000〜+1000 (中立付近は 0)
int sw3 = sbus_switch3(&sbus, 6);       // -1 / 0 / +1
int sw2 = sbus_switch2(&sbus, 4);       // 0 / 1
int raw = sbus_raw(&sbus, 0);           // 生の値 (0〜2047)

// 送信機に合わせた調整 (初期値は 2026_B_767 の値)
sbus_set_stick_range(&sbus, 368, 1024, 1680);  // 最小・中立・最大
sbus_set_deadband(&sbus, 26);                  // 中立から ±26 以内は 0
```

チャンネル番号は 0〜15。範囲外なら 0 を返す。

### 正常の判定 (`sbus_ok`)

`sbus_update` を呼んだ時点で次のどれかに当たれば NG。

1. `sbus_init` から 1 フレームも受信していない (767 の `ch[0] != 0` の代わりに、受信したかどうかの印を使う)
2. 最後のフレームから 200ms 以上空いた (コネクタ抜け)
3. フェイルセーフのビットが立っている (送信機の電源 OFF)

フレーム落ちのビットは、ノイズで立って急停止するので判定に使わない。

### NG のときの値

`sbus_stick`・`sbus_switch2`・`sbus_switch3` は 0 を返す。チェックを書き忘れても止まる方向に倒れるように。`sbus_raw` は最後の値をそのまま返す。

> スイッチの押し直し (OFF にしたら解除) を判定するときは、`sbus_ok` が true のときだけ OFF とみなすこと。起動直後やフェイルセーフ中の 0 を OFF と読むと、ON のままでも解除されてしまう (2026_B_767 で踏んだ落とし穴)。README に書く。

### 値の変換

- **スティック**: 中立 ± 不感帯の内側は 0。外側は中立 → 端を 0 → ±1000 に直線で変換し、±1000 で頭打ちにする。中立の上下で別々に変換するので、範囲が上下非対称な送信機でも中立が 0 になる
- **スイッチ**: 境目はスティックの範囲から自動で決める。`下の境目 = (最小 + 中立) / 2`、`上の境目 = (中立 + 最大) / 2`
  - `sbus_switch3`: 上の境目より大きい → +1、下の境目より小さい → -1、それ以外 → 0
  - `sbus_switch2`: 中立より大きい → 1、それ以外 → 0
  - **生の値が大きいほうが +1**。2026_B_767 の `get_switch_state3` (大きい = -1) とは逆向き

### 中でやること

- `HAL_UARTEx_ReceiveToIdle_DMA` で 64 バイトのバッファに受信し、ハーフ転送の割り込みは切る (767 と同じ)
- 受信イベントで、バッファから 0x0F を探して 25 バイトを 1 フレームとしてデコードし (16ch × 11bit、フラグのバイト)、DMA を再開する。データの途中の 0x0F をヘッダーと間違えないように、**末尾のバイト (先頭から 24 バイト目) が 0x00 か下位 4bit が 4 (SBUS2 の 0x04/0x14/0x24/0x34) のものだけ**を受け入れ、合わなければ次の 0x0F を探す
- UART エラーで受信が止まったら、エラーのコールバックで受信を再開する
- D キャッシュが有効なとき (`SCB->CCR` の DC ビット) は、読む前にバッファのキャッシュを無効化する。そのためバッファは 32 バイト境界に揃え、大きさは 64 バイト
- `sbus_update` は割り込みを止めてから 16ch・フラグ・受信時刻をまとめて写す
- `sbus_t` は 2 つまで登録できる
- RX の DMA が設定されていない (`huart->hdmarx == NULL`) か、Circular (`Init.Mode == DMA_CIRCULAR`) のときは `sbus_init` が `LIB_ERR_IRQ_OFF` を返す (Circular だと受信をかけ直せず、古いフレームを読み続けるため)

## 3. エンコーダー (`mor0koshi_encoder`)

### CubeMX 側の準備

タイマーを Encoder Mode (TI1 and TI2 = 4 逓倍)、Prescaler 0、Period を最大 (16bit なら 65535、TIM2 / TIM5 なら 0xFFFFFFFF) にする。割り込みは要らない。

### 使い方

```c
encoder_t enc1;
encoder_init(&enc1, &htim1, 2048);   // 2048 = 1 回転のカウント数 (4 逓倍込み)
encoder_set_reverse(&enc1, true);    // 向きを逆にしたいとき

// 決まった周期 (1〜10ms くらい) で呼ぶ
encoder_update(&enc1);

int32_t c = encoder_count(&enc1);    // 起動からの累積カウント
int32_t d = encoder_delta(&enc1);    // 前回の update からの変化量
float rpm = encoder_rpm(&enc1);      // 回転数 (符号付き)
float rev = encoder_turns(&enc1);    // 累積の回転数 (count / 1 回転のカウント数)
encoder_reset(&enc1);                // 累積を 0 に戻す
```

### 中でやること

- `encoder_init` で `HAL_TIM_Encoder_Start(htim, TIM_CHANNEL_ALL)` を呼び、今のカウンタ値を基準にする
- **桁あふれ**: `ARR + 1` を一周として、前回との差を `-(一周/2)` 〜 `+(一周/2)` に入るよう折り返す (64bit で計算する)。16bit でも 32bit タイマーでも正しく数える。1 周期の間にタイマーが半周以上進むと数え間違えるが、16bit・2048 カウントで 10ms 周期なら 96000rpm までなので問題ない
- **rpm**: `update` を呼んだ間隔を DWT のサイクルカウンタで測る (`SystemCoreClock` で秒に直す)。`encoder_init` で DWT を有効にする (F7 は LAR のロック解除も行う)。サイクルカウンタは 216MHz で約 19.8 秒で一周するので、前回から `HAL_GetTick` で 10 秒以上空いたときは rpm を 0 にする
- 最初の `update` は `encoder_init` からの間隔で計算する。間隔が 0 なら rpm は前回の値のまま
- 1ms 周期だと遅い回転の分解能が粗い (2048 カウントで 1 カウント ≒ 29rpm)。遅い回転を測るなら 10ms 周期をすすめる (README)
- `update` と値の読み出しは同じ場所 (メインループ同士、または割り込み同士) で行う前提

## 4. エンコーダー CAN (`mor0koshi_encoder_can`)

**エンコーダー 1 個を 1 フレームで**送る。ID は使う側が直接決める。

```c
// 送る側: 10ms ごとに
encoder_can_send(&can1, 0x300, &enc1);

// 受ける側
encoder_can_rx_t roller1;
encoder_can_listen(&can1, 0x300, &roller1);
int32_t c = encoder_can_count(&roller1);
float rpm = encoder_can_rpm(&roller1);
uint32_t age = encoder_can_age_ms(&roller1);   // 一度も届いていなければ UINT32_MAX
```

### フレームの中身 (8 バイト、リトルエンディアン)

| バイト | 型 | 中身 |
|---|---|---|
| 0〜3 | int32 | 累積カウント (`encoder_count`) |
| 4〜7 | float | rpm (`encoder_rpm`) |

- 受ける側は `can_box_t` を内部に持ち、読み出しのたびに割り込みを止めて写す
- 受ける側は 1 回転のカウント数を知らなくても rpm が分かる

### ID と通信量の目安 (README に書く)

- ロボマスが使う 0x1FF〜0x2FF とぶつからないよう 0x300 番台をすすめる
- 1Mbps で 1 フレーム約 0.13ms。6 個を 1ms ごとに送るとバスの約 8 割になるので、10ms ごと (約 8%) をすすめる

## 5. ロボマス (`mor0koshi_robomas`)

### 準備

CAN を 1Mbps にして `can_init` する。ESC (C620 / C610) の ID は本体のボタンで 1〜8 に設定しておく。

### 使い方

```c
robomas_t rm;                                             // 1 本の CAN につき 1 個
robomas_init(&rm, &can1);
robomas_motor_t *wheel = robomas_add(&rm, ROBOMAS_M3508, 1);  // 種類と ESC の ID
robomas_motor_t *arm   = robomas_add(&rm, ROBOMAS_M2006, 5);

// 指令 (最後に呼んだものが有効)
robomas_set_rpm(wheel, 300.0f);     // 速度制御: 出力軸 rpm
robomas_set_angle(arm, 90.0f);      // 位置制御: 出力軸の角度 [度]。720 なら 2 回転
robomas_set_current(wheel, 2000);   // 電流を直接 (M3508: ±16384 / M2006: ±10000 で頭打ち)

// 1ms ごとに 1 回
robomas_update(&rm);                // 全モーターの制御を計算して 0x200 / 0x1FF を送る。can_send の結果を返す

// 読む
float rpm = robomas_rpm(wheel);     // 出力軸 rpm
float deg = robomas_angle(arm);     // 出力軸の累積角度 [度] (最初のフィードバックの位置が 0 度)
robomas_reset_angle(arm);           // 今の位置を 0 度にする
int16_t i = robomas_current(wheel); // 実電流 (生の値)
int t = robomas_temp(wheel);        // 温度 ℃ (M3508 のみ。M2006 は 0)
bool ok = robomas_ok(wheel);        // フィードバックが 100ms 以内に届いているか

// 調整
robomas_set_gear_ratio(m, 1.0f);            // ギア比を変える (外付けギア、ローターで扱いたいとき)
robomas_set_speed_gain(m, kp, ki, i_limit); // 速度 PI
robomas_set_angle_gain(m, kp, max_rpm);     // 位置 P と、そのときの上限 rpm (出力軸)

// 非常時
robomas_stop_all(&rm);              // 全モーターを電流指令 0 にし、積分をリセットする (送るのは次の robomas_update)
```

### CAN の形式 (C620 / C610 共通)

- 指令: ID 0x200 (ESC の ID 1〜4)、0x1FF (ID 5〜8)。各モーター 2 バイトの int16、**ビッグエンディアン**。登録していないモーターの分は 0
- フィードバック: ID 0x200 + ESC の ID。`[0..1]` 角度 0〜8191、`[2..3]` ローター rpm (int16)、`[4..5]` 電流 (int16)、`[6]` 温度 (C620 のみ)。すべてビッグエンディアン
- `robomas_init` は 0x201〜0x208 を `can_subscribe_range` で 1 つの登録として受ける

### 種類ごとの値

| | M3508 (C620) | M2006 (C610) |
|---|---|---|
| ギア比 | 3591/187 (≒ 19.2) | 36 |
| 電流指令の範囲 | ±16384 | ±10000 |
| 速度 PI の初期値 kp / ki / 積分上限 | 1.5 / 0.08 / 5000 (**仮**) | 1.5 / 0.08 / 5000 (AIcontest で確認済み) |
| 位置 P の初期値 kp / 上限 rpm | 2.0 / 100 (**仮**) | 2.0 / 100 (**仮**) |

**仮**の値は実機で確かめていない。README に「実機で調整が要る」と書く。

### 単位

使う側に見えるのは全部**出力軸** (ギアの先) の値。ギア比は種類から自動で入る。

### 制御の中身

- **速度**: PI 制御。目標の出力軸 rpm にギア比を掛けてローター rpm にし、フィードバックのローター rpm との差で計算する。そのため、ギア比を変えてもゲインは変わらない
  - 積分は `update` 1 回ごとに誤差を足す (1ms 周期が前提)
  - 積分は ±積分上限で頭打ち
  - 出力は種類ごとの電流の範囲で頭打ち
- **位置**: `目標 rpm = kp × (目標角度 − 今の角度)` を ±上限 rpm で頭打ちにし、上の速度制御に渡す
- **電流**: そのまま送る (範囲で頭打ち)
- 積分はモーターごとに持つ (AIcontest では 4 個で 1 つを共有していた)
- 制御モードが変わったときと、`robomas_stop_all` のときに積分を 0 にする

### 角度の数え方

生の角度は 1 回転 (0〜8191) で一周する。フィードバックが届くたび (1ms ごと) に**受信割り込みの中で**前回との差を -4096〜+4095 に折り返して足し、何回転でも数える。最高回転 (約 9000rpm) でも 1ms で半回転未満なので数え間違えない。`update` の周期とは関係なく正しく数える。

- 最初のフィードバックで基準を取り、その位置を 0 度にする
- `robomas_reset_angle` は今の累積値を 0 にする

### 安全

- フィードバックが 100ms 届いていないモーターは `robomas_ok` が false になり、`update` で出力を 0 にして積分をリセットする (センサーなしで積分がたまり続け、復帰した瞬間に暴れるのを防ぐ)
- `robomas_stop_all` は SBUS が NG のときに呼ぶ想定で、例のコードにもそう書く。NG のときは `robomas_set_*` を呼ばない (`stop_all` を上書きしてしまう)
- `robomas_update` はメインループでもタイマー割り込みでも呼べる。`robomas_set_*` と `robomas_stop_all` の書き込み (種類 + 目標 + 積分のリセット) と、`update` の全モーターの計算は、`port_irq_save` / `port_irq_restore` で割り込みを止めて行う。`can_send` は自分で止めるので、`update` は計算を終えて割り込みを戻してから呼ぶ
- CAN の断線や ESC の電源断のあいだに回った分は数えられず、累積角度は 1 回の途絶につき最大でローター半周ぶんずれうる。そのあとは `robomas_reset_angle` で原点を取り直す
- `update` は、ID 1〜4 のモーターが 1 つ以上あれば 0x200 を、ID 5〜8 のモーターが 1 つ以上あれば 0x1FF を送る (使っていない範囲のフレームは送らない)

### CAN の通信量 (README に書く)

ロボマスは 1 台ごとに 1ms ごとにフィードバックを返すので、**1 台で 1Mbps の約 13%** を使う。さらに指令のフレーム (0x200、ID 5〜8 を使うなら 0x1FF も) を 1ms ごとに送る。4 台なら フィードバック 4000 + 指令 1000 フレーム/秒で約 65%、5 台は指令が 2 本になって約 91%。**1 本の CAN にはおよそ 4 台まで**にし、それより多いときは CAN2 に分ける。エンコーダーのフレームも同じバスに流すならその分も足す。

## テスト

### PC でのテスト (実機なし)

`test/` で、`test/fake/` の HAL の偽物を使って PC の gcc でライブラリの .c をそのままコンパイルする。`cmake -S test -B build-test && cmake --build build-test && ctest --test-dir build-test` で全部走る。テストを先に書いてから実装する (TDD)。

偽物が用意するもの: `main.h` (HAL の型、`HAL_*_MODULE_ENABLED`)、送信・受信・タイマー・DMA 開始の HAL 関数 (呼ばれた内容を記録する)、`HAL_GetTick`、DWT のサイクルカウンタ、`SystemCoreClock`、`NVIC_GetEnableIRQ`、`SCB->CCR`、割り込みの禁止・許可。

| 部品 | 確かめること |
|---|---|
| SBUS | 既知のフレーム → 16ch の値、先頭のゴミを飛ばす、途中の 0x0F を読み飛ばす (末尾のバイトの確認、SBUS2 の末尾)、Circular の DMA を断る、正常の判定 (未受信・200ms 途絶・フェイルセーフ・フレーム落ちは無視)、スティックの変換 (不感帯・非対称・頭打ち)、スイッチの境目、NG のときは 0、UART エラーで受信を再開 |
| CAN | ID ごとの振り分け (箱・関数・範囲・重なり)、知らない ID は捨てる、4 フレーム以上送ると列に入り完了割り込みで順に出る、あふれたら `LIB_ERR_FULL` と捨てた数、`put` / `get` の往復、CAN2 のフィルタ番号 14、割り込みがオフなら `LIB_ERR_IRQ_OFF` |
| エンコーダー | 16bit・32bit の桁あふれ (両方向)、逆向き、rpm (偽の DWT で時間を進める)、10 秒以上空いたら rpm 0、リセット |
| エンコーダー CAN | 送ったフレームの中身、受けた値の取り出し (負の値も)、未受信の age |
| ロボマス | 指令と update が割り込みを止めて動く (終わったら戻す)、フィードバックの解釈 (ビッグエンディアン)、何回転してもの角度 (両方向・一周またぎ・最初は 0 度)、出力軸への換算、0x200 / 0x1FF の中身 (使っていない範囲のフレームは送らない)、電流の頭打ち、積分の頭打ち、途絶 → 出力 0 と積分リセット、`stop_all`、モード切替で積分リセット、同じ ID の二重登録は NULL |

### ARM でのビルド確認

本物の CubeMX プロジェクトのコンパイル設定で、ライブラリと例 3 つが**警告なし** (`-Wall -Wextra`) でビルドできることを確かめる。プロジェクトはスクラッチ領域にコピーして使い、元のリポジトリには触らない。

1. F767 (2026_B_767 の設定)。コンパイルだけ (767 は HAL の CAN・UART コールバックを自分で書いているので、リンクすると衝突する)
2. F446 (2026_B_446 の設定)。リンクまで行い、HAL のコールバックがライブラリのものになっていることを `arm-none-eabi-nm` で確かめる。AIcontest も HAL のコールバックを自分で書いているので使わない
3. CAN を使っていない F767 (mortor_test の設定)。リンクまで行い、CAN の部分が外れること

例はライブラリには入れず、ビルド確認のときだけ一緒にコンパイルする。各例は `example_xxx_init()` と `example_xxx_loop()` の 2 つの関数として書く。

### 実機での確認 (使う人が行う。README にチェックリストを付ける)

- SBUS: 値の表示、コネクタを抜くと 200ms 以内に NG、送信機の電源を切ると NG
- CAN: 2 枚の基板で送って受ける
- エンコーダー: 手で 1 回転させて `encoder_turns` がおよそ 1.0
- ロボマス: タイヤを浮かせて小さい rpm で回す → `robomas_set_angle(m, 360)` で 1 回転して止まる → ゲインを調整
