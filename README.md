# dinosaur_library

STM32 のロボット用ライブラリです。ライブラリというのは、よく使う処理をまとめた「部品集」のことです。
プロポの受信 (SBUS)、エンコーダー、CAN 通信、ロボマスモーター (M3508 / M2006) を、
**最初に `init` を 1 回呼んで、ループで `update` を呼ぶだけ**で使えるようにします。

たとえば、プロポのスティックの値を読むのはこれだけです。自分で書くと 60 行くらいかかる処理です。

```c
sbus_init(&sbus, &huart5);        // 最初に 1 回：受信を始める
sbus_update(&sbus);               // ループで毎回：最新の値を写し取る
int ly = sbus_stick(&sbus, 1);    // スティックの値 (-1000〜+1000)
```

| 部品 | 関数の頭 | できること |
|---|---|---|
| **SBUS** | `sbus_` | プロポの受信。スティックを −1000〜+1000、スイッチを −1 / 0 / +1 に変換する。電波が切れたかどうかもわかる |
| **エンコーダー** | `encoder_` | 回転数 [rpm]、何回転したか、起動してからのカウント |
| **CAN** | `can_` | 好きな ID でデータを送る・受け取る。相手から届かなくなったかどうかもわかる |
| **エンコーダー CAN** | `encoder_can_` | エンコーダーの値を、CAN で別の基板に送る |
| **ロボマス** | `robomas_` | M3508 / M2006 を、回転数や角度を指定して動かす |

使える関数は全部、このページの下の **[関数の一覧](#関数の一覧)** に、できることといっしょにまとめてあります。

- 使えるマイコン: STM32 の F4 / F7 (F446RE・F767ZI でビルドを確認。実機での確認はこれから)
- 言語: C (CubeMX で作ったプロジェクトに、そのまま足せます)

> [!TIP]
> このライブラリの中では、SBUS の受信、エンコーダーの読み取り、CAN の送受信を、講習で自分で書くのと同じやり方でやっています。
> 中身を知らないと、動かないときに原因を探せません。**一度は自分で書いて動かしてから**、ライブラリに乗り換えるのがおすすめです。

---

## はじめに知っておくことば

プログラミングを始めたばかりの人向けに、このページに出てくることばをまとめました。わからなくなったら戻ってきてください。

| ことば | 意味 | このライブラリでの例 |
|---|---|---|
| **関数** | 名前を呼ぶと、決まった仕事をしてくれるもの | `sbus_init(...)`、`sbus_stick(...)` |
| **引数** (ひきすう) | 関数に渡す材料。`( )` の中に `,` で区切って書く | `sbus_stick(&sbus, 1)` の `&sbus` と `1` |
| **戻り値** | 関数が仕事のあとに返してくれる答え | `sbus_stick` はスティックの値を返す |
| **変数** | 値を入れておく「入れ物」 | `int ly = ...;` の `ly` |
| **型** | 入れ物の種類。`int` は整数、`float` は小数 | `sbus_t` は「SBUS の情報をまとめて入れる入れ物」の種類 |
| **`&`** (アンド) | 入れ物の「場所」(アドレス) を渡す印 | `sbus_init(&sbus, &huart5)` |
| **ポインタ `*`** | 場所を覚えておく変数 | `robomas_motor_t *wheel` |
| **`true` / `false`** | 「正しい」「正しくない」。数字にすると 1 と 0 | `sbus_ok(&sbus)` の答え |
| **init / update** | init は最初に 1 回だけやる準備。update はループで毎回やる更新 | どの部品も同じ形 |
| **割り込み** | 何か起きたとき (データが届いたなど)、今の仕事を中断して呼ばれる関数 | 受信はライブラリが割り込みで自動でやる |
| **HAL** | ST が用意した、マイコンを動かすための関数の集まり | `HAL_Delay(100)` など |
| **ハンドル** | CubeMX が作る、周辺機能 (UART・CAN・タイマー) の名前 | `huart5` (UART5)、`hcan1` (CAN1)、`htim4` (TIM4) |

### なぜ `&` を付けるの？

C 言語の関数は、渡された値の**コピー**を受け取ります。`sbus_init(sbus, ...)` のように入れ物そのものを渡すと、
関数はコピーに書き込むだけで、あなたの `sbus` は変わりません。

そこで `&sbus` と書いて「入れ物はここにあるよ」と**場所**を教えます。住所を教えて荷物を届けてもらうのと同じで、
関数はその場所にある本物の `sbus` に書き込めます。このライブラリの関数は、最初の引数にいつも `&入れ物` を渡します。

```c
sbus_t sbus;                 // SBUS 用の入れ物を 1 つ作る (この時点では中身は空っぽ)
sbus_init(&sbus, &huart5);   // 「sbus の場所」と「huart5 の場所」を渡して、準備してもらう
```

### ポインタ `*` って？

`robomas_motor_t *wheel;` の `*` は、「`wheel` には、モーターの情報が入っている**場所**を入れる」という意味です。
`robomas_add` は、登録したモーターの場所を返してくれるので、それを `wheel` に入れておきます。
あとは `robomas_set_rpm(wheel, 100.0f)` のように、`&` を付けずにそのまま渡します (もう場所が入っているから)。

---

## 準備 1：ライブラリを取ってくる

### A. Git を使う (おすすめ)

VS Code の「ターミナル › 新しいターミナル」を開いて、プロジェクトを置いているフォルダに移動してから、次の 2 行を打ちます。

```bash
cd C:\STM32_Projects
git clone https://github.com/mor0koshi/dinosaur_library.git
```

ライブラリが新しくなったときは、`dinosaur_library` フォルダの中で `git pull` を打つと更新できます。

> [!NOTE]
> 「git は、内部コマンドまたは外部コマンド…として認識されていません」と出たら、Git が入っていません。
> 下の B のやり方にするか、[git-scm.com](https://git-scm.com/) から Git for Windows を入れてから、VS Code を開き直してください。

### B. ZIP で落とす

このページの上の緑の「Code」ボタン →「Download ZIP」を押します。展開してできた `dinosaur_library-main` フォルダの
**名前の「-main」を消して** `dinosaur_library` にし、プロジェクトを置いているフォルダに入れます。

### 置く場所

ライブラリは、**自分のプロジェクトと同じ階層に 1 つだけ**置きます。プロジェクトをいくつ作っても、全部このライブラリを使えます。

```text
C:\STM32_Projects\
  dinosaur_library\     ← ライブラリ (名前はこのとおり)
    include\             ← 使う側が読むファイル (関数の一覧)
    src\                 ← ライブラリの中身
    examples\            ← 使い方の例
    README.md            ← この説明書
  lib_test\              ← 自分のプロジェクト
```

> [!WARNING]
> フォルダの場所の名前に、日本語やスペースが入っているとビルドが失敗します (`C:\Users\山田 太郎\...` など)。
> `C:\STM32_Projects` のように、半角英数字だけの場所に置いてください。

---

## 準備 2：プロジェクトに組み込む

CubeMX で作ったプロジェクトに、3 か所書き足します。

### ① CMakeLists.txt に 2 行足す

プロジェクトのいちばん上の階層にある `CMakeLists.txt` を開いて、「← この行を足す」の 2 行を足します。ほかの行はそのままです。

```cmake
# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(../dinosaur_library dinosaur_library)    # ← この行を足す

# (このあいだの行はそのまま)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    dinosaur_library                                       # ← この行を足す

    # Add user defined libraries
)
```

| 足した行 | 意味 |
|---|---|
| `add_subdirectory(../dinosaur_library dinosaur_library)` | 「1 つ上のフォルダにある dinosaur_library もいっしょにビルドして」。`../` は 1 つ上の階層のこと。だからライブラリはプロジェクトの隣に置く |
| `dinosaur_library` (target_link_libraries の中) | できあがったライブラリを、自分のプログラムにつなげる |

ライブラリは、あなたのプロジェクトと同じ設定 (マイコンの種類など) でビルドされます。だから F446RE でも F767ZI でも同じものが使えます。
CubeMX で使っていない機能 (たとえば CAN) の部品は、自動でビルドから外れます。

> [!NOTE]
> `CMakeLists.txt` は、CubeMX が最初の 1 回だけ作るファイルです。あとから CubeMX で「GENERATE CODE」を押しても上書きされないので、足した 2 行は消えません。

### ② main.c で include する

`main.c` の `USER CODE BEGIN Includes` の中に書きます。これ 1 行で、全部の部品が使えるようになります。

```c
/* USER CODE BEGIN Includes */
#include <stdio.h>                 // printf を使うため (講習 02)
#include "dinosaur_library.h"     // ライブラリを使うため
/* USER CODE END Includes */
```

### ③ ビルドして確かめる

ビルドして「Build finished with exit code 0」と出れば、組み込みは完了です。
まだライブラリの関数を呼んでいないので、ボードの動きは変わりません。

---

## 準備 3：main.c のどこに書くか

CubeMX で作った `main.c` には、`/* USER CODE BEGIN ○○ */` と `/* USER CODE END ○○ */` で囲まれた場所がいくつかあります。
**自分のコードは、必ずこの BEGIN と END のあいだに書きます** (それ以外の場所は、CubeMX で作り直すと消えます)。

ライブラリを使うときは、どの部品でも次の 3 か所に書きます。

| 書く場所 | 書くこと | 例 |
|---|---|---|
| `USER CODE BEGIN PV` | 部品の入れ物 (変数) を作る | `sbus_t sbus;` |
| `USER CODE BEGIN 2` | `init` を 1 回だけ呼ぶ | `sbus_init(&sbus, &huart5);` |
| `while (1)` の中 (`USER CODE BEGIN WHILE` のあたり) | `update` を呼んでから値を読む | `sbus_update(&sbus);` |

全体の形はこうなります (CubeMX が作った部分は省略しています)。

```c
/* USER CODE BEGIN PV */
sbus_t sbus;                          // ① 入れ物を作る (関数の外なので、どこからでも使える)
/* USER CODE END PV */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_DMA_Init();                      // CubeMX が作った初期化。ここでピンや UART が使えるようになる
  MX_UART5_Init();
  /* USER CODE BEGIN 2 */
  sbus_init(&sbus, &huart5);          // ② init は 1 回だけ。CubeMX の初期化のあとに呼ぶ
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
    sbus_update(&sbus);               // ③ ループで毎回 update
    int ly = sbus_stick(&sbus, 1);    //    そのあとで値を読む
    printf("ly=%d\r\n", ly);
    HAL_Delay(100);
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}
```

> [!IMPORTANT]
> - `init` を `while (1)` の中に書かないでください。毎回やり直しになって、正しく動きません
> - `update` を呼び忘れると、値がずっと変わりません
> - `init` は、CubeMX が作った初期化 (`MX_◯◯_Init()`) より**あと** (`USER CODE BEGIN 2`) に書きます。先に呼ぶと、UART などがまだ準備できていません
> - 入れ物 (`sbus_t sbus;` など) は、必ず**関数の外** (`USER CODE BEGIN PV`) に書きます。
>   自分で作った関数の中に書くと、その関数が終わったときに入れ物が消えて、割り込みが消えた場所に書き込んでしまいます

---

## SBUS (プロポの受信)

受信機から届くプロポの操作を読み取ります。

### CubeMX の設定

講習 04 と同じです。左の一覧で「Connectivity › UART5」を開いて設定します。

| タブ | 項目 | 設定 |
|---|---|---|
| (上) | Mode | Asynchronous |
| Parameter Settings | Baud Rate | 100000 |
| Parameter Settings | Word Length | 9 Bits (パリティ込み) |
| Parameter Settings | Parity / Stop Bits | Even / 2 |
| Advanced Parameters | RX Pin Active Level Inversion | Enable (F767ZI)。F446RE には無いので、基板に反転回路をつける |
| DMA Settings | Add → UART5_RX | **Mode は Normal** (Circular にすると `sbus_init` が失敗します) |
| NVIC Settings | UART5 global interrupt | Enabled にチェック |

### いちばん簡単な例

```c
/* USER CODE BEGIN PV */
sbus_t sbus;                            // SBUS 用の入れ物

/* USER CODE BEGIN 2 */
sbus_init(&sbus, &huart5);              // 受信を始める

/* USER CODE BEGIN WHILE */
while (1)
{
  sbus_update(&sbus);                   // 最新の値を写し取る
  printf("ok=%d  stick=%5d  sw=%2d  raw=%4d\r\n",
         sbus_ok(&sbus),                // 受信できているか (1 / 0)
         sbus_stick(&sbus, 1),          // スティック (-1000〜+1000)
         sbus_switch3(&sbus, 4),        // 3 段スイッチ (-1 / 0 / +1)
         sbus_raw(&sbus, 1));           // 生の値 (スティックならだいたい 368〜1680)
  HAL_Delay(100);
```

シリアルモニターに `ok=1` と出て、スティックを動かすと `stick` が −1000〜+1000 のあいだで変われば成功です。

### 1 行ずつの説明

| コード | 何をしているか |
|---|---|
| `sbus_t sbus;` | SBUS の情報 (届いた値、受信できているか、など) をまとめて入れる入れ物を作る |
| `sbus_init(&sbus, &huart5);` | UART5 で受信を始める。データが届くと、割り込みでライブラリが自動で受け取って、`sbus` の中に書いておいてくれる。ノイズで受信が止まっても、自動で受信し直す |
| `sbus_update(&sbus);` | 割り込みで届いた最新の値を、使う用の場所に写し取る。読んでいる途中で値が変わらないようにするため。値を読む前に毎回呼ぶ |
| `sbus_ok(&sbus)` | うまく受信できていれば 1 (true)、だめなら 0 (false) |
| `sbus_stick(&sbus, 1)` | スティックの値。真ん中で 0、端で ±1000 |
| `sbus_switch3(&sbus, 4)` | 3 段スイッチ。−1 / 0 / +1 |
| `sbus_switch2(&sbus, 4)` | 2 段スイッチ。0 / 1 |
| `sbus_raw(&sbus, 1)` | 生の値 (届いた数字そのまま)。講習 04 で見た値と同じ。スティックならだいたい 368〜1680、スイッチは段ごとに決まった値で、チャンネルやプロポによって違う |

> [!IMPORTANT]
> **チャンネルの番号は 0 から数えます。** 講習 04 の `SBUS_CH[番号]` と同じ番号です。
> `sbus_stick(&sbus, 0)` が CH1、`sbus_stick(&sbus, 1)` が CH2 です。

### 受信できていないときの値

`sbus_ok` が 0 になるのは、次のどれかのときです。

1. 電源を入れてから、まだ 1 回もデータが届いていない
2. 0.2 秒 (200ms) 以上データが届いていない (受信機のコネクタが抜けた、など)
3. 受信機が「送信機を見失った」と知らせてきた (送信機の電源を切った、電波が届かない)

このとき、`sbus_stick` と `sbus_switch2` / `sbus_switch3` は **0** を返します。
つまり、電波が切れると「スティックを離した」のと同じになるので、ロボットは止まる方向になります。
`sbus_raw` だけは、最後に届いた値をそのまま返します。

> [!WARNING]
> **「スイッチを OFF に戻したら解除」「ON にした瞬間に 1 回だけ動かす」のような処理は、`sbus_ok` が 1 のときだけにしてください。**
> 電波が切れている間はスイッチが全部 0 (OFF) と読めます。それを「OFF にした」と思ってしまうと、
> スイッチを ON にしたままでも解除されてしまいます。

たとえば「スイッチ (CH5) を ON にした瞬間に 1 回だけ発射する」処理で、電波が一瞬切れると次のようになります。

| 時間 | 本当のスイッチ | 電波 | `sbus_switch2` | プログラムの判断 |
|---|---|---|---|---|
| 0 秒 | ON | ○ | 1 | OFF → ON になった → **発射** |
| 1 秒 | ON のまま | × 切れた | 0 | 「OFF に戻した」と思う |
| 2 秒 | ON のまま | ○ 戻った | 1 | 「OFF → ON にした」と思う → **もう一度発射** (操縦者は何もしていない) |

前の値を覚えておく変数は、**`sbus_ok` が 1 のときだけ**書き換えます。こうすると、電波が切れている間の 0 を「OFF にした」と数えません。

```c
static int prev_sw = 0;                     // 前のスイッチの状態 (static なので、ループを回っても値が消えない)
if (sbus_ok(&sbus)) {                       // 受信できているときだけ
  int sw = sbus_switch2(&sbus, 4);          // 今のスイッチ (CH5)
  if (sw == 1 && prev_sw == 0) {
    // OFF → ON にした瞬間：ここに 1 回だけやることを書く
  }
  prev_sw = sw;                             // 前の状態を書き換えるのも、受信できているときだけ
}
```

### 送信機に合わせる

最初は Futaba のプロポの値 (いちばん小さい値 368・真ん中 1024・いちばん大きい値 1680) になっています。
`sbus_raw` で見た値が違うときは、`sbus_init` のすぐあとに書き足します。

```c
sbus_init(&sbus, &huart5);
sbus_set_stick_range(&sbus, 368, 1024, 1680);   // いちばん小さい値・真ん中・いちばん大きい値
sbus_set_deadband(&sbus, 26);                   // 真ん中から ±26 以内は 0 にする (手ぶれ対策)
```

スイッチの境目も、この 3 つの値から自動で決まります (真ん中と端のちょうど中間)。
スイッチの向き (上が +1 か −1 か) が思っていたのと逆なら、使う側で `-sbus_switch3(&sbus, 4)` のように符号を反対にします。

### プロポの値がずれているとき

長く使ったプロポや、落として壊れかけたプロポは、**スティックごとに**値がずれることがあります。たとえば次のような症状です。

- スティックを離しても `sbus_stick` が 0 にならない (真ん中がずれている)
- 端まで倒しても ±1000 にならない (端まで値が出ない)
- 3 段スイッチの真ん中や端が、正しく読めない

そのときは、**ずれているチャンネルだけ** 値を直せます。手順は次のとおりです。

**① 生の値を見る。** 直したいチャンネル (ここでは CH2 = 番号 `1`) の `sbus_raw` を表示します。

```c
sbus_update(&sbus);
printf("raw=%4d\r\n", sbus_raw(&sbus, 1));
HAL_Delay(100);
```

**② 3 つの値をメモする。** スティックを離したとき (真ん中) と、両側の端まで倒したときの値を読みます。
端の 2 つは、小さい方が「最小」、大きい方が「最大」です。たとえば「真ん中 1040、最小 400、最大 1600」だったとします。

**③ `sbus_init` のすぐあとに書く。**

```c
sbus_init(&sbus, &huart5);
sbus_set_channel_range(&sbus, 1, 410, 1040, 1590);   // CH2 だけ：いちばん小さい値・真ん中・いちばん大きい値
sbus_set_channel_deadband(&sbus, 1, 40);             // CH2 だけ：真ん中から ±40 以内は 0 (真ん中がふらつくとき)
```

これで CH2 は、離すと 0、端まで倒すと ±1000 になります。ほかのチャンネルは今までどおりです。
その CH の 3 段スイッチ・2 段スイッチの境目も、書いた 3 つの値から決まります。

| 関数 | 直すもの |
|---|---|
| `sbus_set_channel_range(&sbus, ch, 最小, 真ん中, 最大)` | そのチャンネルだけの、いちばん小さい値・真ん中・いちばん大きい値 |
| `sbus_set_channel_deadband(&sbus, ch, 幅)` | そのチャンネルだけの不感帯 (真ん中から ±幅 は 0) |

> [!TIP]
> - 端の値は、メモした値より **10 くらい内側** (上の例なら 400 → 410、1600 → 1590) を書くと、毎回きちんと ±1000 まで届きます。
>   書いた値より外側は、±1000 で止まります
> - 真ん中の値が毎回少し違うときは、何回か離してみて、いちばんよく出る値を真ん中にし、ふらつく幅より少し大きい不感帯にします
> - 全部のチャンネルをまとめて決める `sbus_set_stick_range` / `sbus_set_deadband` は、チャンネルごとの設定も上書きします。
>   両方使うときは、**まとめての方を先に**、チャンネルごとの方をあとに書きます
> - 書けるのは SBUS で送れる数字の範囲 (0〜2047) で、「最小 < 真ん中 < 最大」にします。おかしな値を書くと何も変わらず、`lib_error_hook` に `LIB_ERR_PARAM` が来ます

### スティックでモーターの速さを決める例

スティックの −1000〜+1000 を、PWM の −255〜+255 に直します (講習 03・05)。

```c
int v = sbus_stick(&sbus, 1) * 255 / 1000;   // 先に掛けてから割る
```

`sbus_stick(&sbus, 1) / 1000 * 255` の順だと、`500 / 1000` が 0 になってしまいます
(整数どうしの割り算は、小数点以下が切り捨てられるため)。**掛け算を先に**書きましょう。

### よくある間違い

| 書いたコード | どうなる | 直し方 |
|---|---|---|
| `sbus_init(sbus, huart5);` | ビルドエラー | `&` を付ける：`sbus_init(&sbus, &huart5);` |
| `sbus_update` を書いていない | 値がずっと 0 のまま | ループの中で毎回呼ぶ |
| `sbus_init` を `while` の中に書いた | ほとんど受信できない | `USER CODE BEGIN 2` に移す |
| `sbus_stick(&sbus, 2)` で CH2 を読もうとした | 1 つ隣のチャンネルが読める | 番号は 0 から：CH2 は `1` |

---

## エンコーダー (回転数を測る)

### CubeMX の設定

講習 07 のエンコーダー用タイマー (例：TIM4) だけ設定します。**一定時間ごとの割り込み (TIM6) はいりません。**

- 「Combined Channels」を「Encoder Mode」にする
- Parameter Settings の「Encoder Mode」を「Encoder Mode TI1 and TI2」(4 逓倍) にする
- Prescaler は 0、Counter Period は 65535 (TIM2 / TIM5 なら 4294967295)
- `HAL_TIM_Encoder_Start` は `encoder_init` がやるので、書かなくてかまいません (講習 07 のコードに残っていても動きます)

### いちばん簡単な例

```c
/* USER CODE BEGIN PV */
encoder_t enc;                                // エンコーダー用の入れ物

/* USER CODE BEGIN 2 */
encoder_init(&enc, &htim4, 2048 * 4);         // 1 回転のカウント数 = PPR × 4

/* USER CODE BEGIN WHILE */
uint32_t last = 0;                            // 前に update した時刻
uint32_t last_print = 0;                      // 前に表示した時刻
while (1)
{
  if (HAL_GetTick() - last >= 20) {           // 20ms たったら
    last = HAL_GetTick();
    encoder_update(&enc);                     // タイマーを読んで計算する
  }
  if (HAL_GetTick() - last_print >= 100) {    // 100ms たったら
    last_print = HAL_GetTick();
    printf("rpm=%5d  count=%ld\r\n", (int)encoder_rpm(&enc), (long)encoder_count(&enc));
  }
```

### 1 行ずつの説明

| コード | 何をしているか |
|---|---|
| `encoder_init(&enc, &htim4, 2048 * 4);` | TIM4 でカウントを始める。3 つ目の引数は「1 回転で何カウントか」。A 相・B 相の両方の変化を数える (4 逓倍) ので、エンコーダーの分解能 (PPR) × 4 |
| `HAL_GetTick() - last >= 20` | 起動してからのミリ秒 (`HAL_GetTick()`) と前の時刻の差が 20 以上なら、つまり 20ms たったら |
| `encoder_update(&enc);` | タイマーを読んで、前回からの差を計算する。決まった間隔 (1〜20ms くらい) で呼ぶ |
| `encoder_rpm(&enc)` | 回転数 [rpm]。逆回転ならマイナス。小数 (float) なので、`%d` で表示するときは `(int)` を付ける |
| `encoder_count(&enc)` | 起動してからのカウントの合計。タイマーが 65535 → 0 と一周しても、正しく数え続ける |

ほかに、こんな関数もあります。

| 関数 | 返すもの |
|---|---|
| `encoder_turns(&enc)` | 何回転したか (小数) |
| `encoder_delta(&enc)` | 前回の update からのカウントの差 (講習 07 の diff と同じ) |
| `encoder_reset(&enc)` | カウントの合計を 0 に戻す (原点合わせ) |
| `encoder_set_reverse(&enc, true)` | 向きを逆にする (A 相と B 相の線を入れ替える代わり) |

> [!TIP]
> 回転数は、ライブラリが CPU の時計で「前の update から何秒たったか」を測って計算します。
> だから update を呼ぶ間隔がぴったり 20ms でなくても大丈夫です。
> ただし、1ms ごとのように短くしすぎると、1 カウントの違いで rpm が大きく変わって値がガタガタします。10〜20ms がおすすめです。

---

## CAN (基板どうしの通信)

### CubeMX の設定

講習 08 と同じ設定 (CAN1、通信速度 1Mbps) に、1 つ足します。

- NVIC Settings で **「CAN1 RX0 interrupt」と「CAN1 TX interrupts」の両方**にチェック
  (ライブラリは「送り終わった」という合図を使って、次のデータを送ります)
- フィルタの設定、`HAL_CAN_Start`、`HAL_CAN_ActivateNotification` は**書きません** (`can_init` が全部やります)
  - 講習 08 のコードの `HAL_CAN_Start` が残っていても、`can_init` がいったん止めてやり直すので動きます
  - **フィルタの設定 (`HAL_CAN_ConfigFilter`) は消してください。** 残っていると、ライブラリが受け取れない ID が出ます
- 1 枚で試すときは Operating Mode を「Loopback」、2 枚でつなぐときは「Normal」

### いちばん簡単な例 (1 枚の Loopback で、送ったものを自分で受け取る)

```c
/* USER CODE BEGIN PV */
can_t can1;                           // CAN1 用の入れ物
can_box_t box;                        // 受け取ったデータをしまっておく箱

/* USER CODE BEGIN 2 */
can_init(&can1, &hcan1);              // CAN を使えるようにする (フィルタ・開始・割り込み)
can_listen(&can1, 0x001, &box);       // ID 0x001 が届いたら box にしまう

/* USER CODE BEGIN WHILE */
int16_t count = 0;
while (1)
{
  uint8_t r[8];                       // 受け取ったデータを入れる 8 バイト
  if (can_read(&box, r)) {            // 新しく届いていたら
    printf("recv %d\r\n", can_get_i16(r, 0));   // 0〜1 バイト目を整数として取り出す
  }

  uint8_t d[8] = {0};                 // 送るデータ 8 バイト (全部 0 で始める)
  can_put_i16(d, 0, count);           // 0〜1 バイト目に count を入れる
  count++;
  can_send(&can1, 0x001, d, 8);       // ID 0x001 で 8 バイト送る
  HAL_Delay(500);
```

`recv 0`、`recv 1`、`recv 2`… と 1 ずつ増えていけば成功です。

### 1 行ずつの説明

| コード | 何をしているか |
|---|---|
| `can_init(&can1, &hcan1);` | 講習 08 で書いた、フィルタの設定・`HAL_CAN_Start`・受信の割り込みの 10 行の代わり。ケーブルが抜けて CAN が止まっても、自動で復帰する |
| `can_listen(&can1, 0x001, &box);` | 「ID 0x001 のデータが届いたら、`box` にしまっておいて」と頼む。箱には最新の 1 回分だけ入る |
| `can_read(&box, r)` | 前に読んだあとに新しく届いていたら、データを `r` に写して true を返す。届いていなければ false |
| `can_send(&can1, 0x001, d, 8);` | ID 0x001 でデータ `d` を 8 バイト送る。すぐに送れないときは、ライブラリの中で順番待ちをしてから、送った順に送る |
| `can_put_i16(d, 0, count);` | `count` (2 バイトの整数) を、`d` の 0 バイト目から入れる |
| `can_get_i16(r, 0)` | `r` の 0 バイト目から、2 バイトの整数を取り出す |

### データの詰め方

CAN の 1 回分のデータは 8 バイトです。「何バイト目に、何の値を入れるか」を、送る側と受け取る側で同じにします。

| 関数 | 入れられる値 | 使うバイト数 |
|---|---|---|
| `can_put_u8` / `can_put_i8` | 0〜255 / −128〜127 | 1 |
| `can_put_u16` / `can_put_i16` | 0〜65535 / −32768〜32767 | 2 |
| `can_put_u32` / `can_put_i32` | とても大きい整数 | 4 |
| `can_put_float` | 小数 | 4 |

取り出すときは、同じ名前の `can_get_◯◯` を使います。たとえば、左右のスティックを送るなら次のように決めます。

```c
// 送る側
can_put_i16(d, 0, sbus_stick(&sbus, 1));   // 0〜1 バイト目：左スティック
can_put_i16(d, 2, sbus_stick(&sbus, 2));   // 2〜3 バイト目：右スティック
can_send(&can1, 0x100, d, 8);

// 受け取る側
int left  = can_get_i16(r, 0);
int right = can_get_i16(r, 2);
```

### 相手から届かなくなったら止める

```c
if (can_age_ms(&box) > 100) {      // 最後に届いてから 100ms 以上たった
  // 相手の基板の電源が切れた、線が抜けた → モーターを止める
}
```

> [!IMPORTANT]
> `can_init` は **1 つの CAN につき 1 回だけ**、`can_listen` などより**先に**呼びます。
> - `can_init` より先に `can_listen` を呼ぶと、登録されずに `LIB_ERR_PARAM` になります
> - `can_init` をあとからもう一度呼ぶと、それまでの `can_listen` の登録が消えます

---

## エンコーダーの値を別の基板に送る

エンコーダーを読む基板と、モーターを回す基板が違うときに使います。エンコーダー 1 個を 1 回のデータ
(0〜3 バイト目：カウントの合計、4〜7 バイト目：rpm) で送ります。

```c
// 送る側 (エンコーダーの基板)：10ms ごとに
if (HAL_GetTick() - last >= 10) {
  last = HAL_GetTick();
  encoder_update(&enc);
  encoder_can_send(&can1, 0x300, &enc);      // ID 0x300 で送る
}
```

```c
// 受け取る側
/* USER CODE BEGIN PV */
encoder_can_rx_t wheel;                      // 受け取った値の入れ物

/* USER CODE BEGIN 2 */
can_init(&can1, &hcan1);
encoder_can_listen(&can1, 0x300, &wheel);    // ID 0x300 を受け取る

/* USER CODE BEGIN WHILE */
float rpm = encoder_can_rpm(&wheel);         // 送られてきた rpm
if (encoder_can_age_ms(&wheel) > 100) {      // 100ms 届かなければ
  // 線が抜けたとみなしてモーターを止める
}
```

---

## ロボマスモーター (M3508 / M2006)

DJI の RoboMaster のモーターは、専用のドライバ (ESC：M3508 なら C620、M2006 なら C610) と CAN でつながります。
マイコンは CAN で「この電流を流して」と送り、ESC は 1ms ごとに「今の角度・回転数・電流」を返してきます。
ライブラリは、それを使って**回転数 (rpm) や角度を指定して動かす**ところまでやります。

### 準備

1. **ESC の ID を決める**：ESC のボタンで 1〜8 の ID を設定する (やり方は ESC の説明書どおり)。同じ CAN の線の上で ID がかぶらないようにする
2. **配線**：マイコン → CAN トランシーバ → CANH・CANL → ESC。線の両端に 120Ω の終端抵抗。ESC の電源はバッテリーから (電圧は ESC の説明書どおり)
3. **CubeMX**：CAN の設定は上の「CAN」と同じ (1Mbps、RX0 と TX の割り込み)。Operating Mode は **Normal**

### プロポで M3508 を回す例

```c
/* USER CODE BEGIN PV */
can_t can1;
sbus_t sbus;
robomas_t rm;                                  // ロボマス全体の入れ物 (CAN 1 本に 1 つ)
robomas_motor_t *wheel;                        // モーター 1 台の場所を覚えておく

/* USER CODE BEGIN 2 */
can_init(&can1, &hcan1);
sbus_init(&sbus, &huart5);
robomas_init(&rm, &can1);                      // ロボマスの受信を始める
wheel = robomas_add(&rm, ROBOMAS_M3508, 1);    // M3508 を ESC の ID 1 で登録

/* USER CODE BEGIN WHILE */
uint32_t last = 0;
while (1)
{
  if (HAL_GetTick() != last) {                 // 1ms に 1 回
    last = HAL_GetTick();
    sbus_update(&sbus);
    if (!sbus_ok(&sbus)) {                     // 受信できていなければ
      robomas_stop_all(&rm);                   //   全部止める
    } else {                                   // 受信できていれば
      robomas_set_rpm(wheel, sbus_stick(&sbus, 1) * 0.3f);   // スティックで ±300rpm
    }
    robomas_update(&rm);                       // 止めているときも、毎回必ず呼ぶ
  }
```

### 1 行ずつの説明

| コード | 何をしているか |
|---|---|
| `robomas_init(&rm, &can1);` | ESC からの返事 (角度・回転数) を受け取り始める。`can_init` のあとに呼ぶ (先に呼ぶと `LIB_ERR_PARAM`) |
| `robomas_add(&rm, ROBOMAS_M3508, 1)` | モーターを登録して、その場所を返す。種類は `ROBOMAS_M3508` か `ROBOMAS_M2006`。ID が 1〜8 以外・同じ ID を 2 回登録・`robomas_init` より先に呼んだときは NULL (失敗) |
| `robomas_set_rpm(wheel, 回転数)` | 「この回転数で回って」と指示する。単位は出力軸 (ギアの先) の rpm |
| `robomas_update(&rm);` | 登録した全部のモーターについて、指示に近づけるための電流を計算して ESC に送る。**1ms ごとに 1 回**呼ぶ。同じ 1ms のあいだに何回呼んでも、計算して送るのは 1 回だけ |
| `robomas_stop_all(&rm);` | 全部のモーターを止める (電流 0)。実際に送るのは次の `robomas_update` なので、止めているあいだも update は呼び続ける |

> [!WARNING]
> 受信できていないときに `robomas_set_rpm` などを呼ばないでください。`robomas_stop_all` の「止めて」が上書きされて、また動いてしまいます。
> 上の例のように、`if (!sbus_ok(&sbus))` で分けて、どちらか片方だけを呼びます。

### 使える指示と、読める値

値は全部**出力軸** (ギアの先、タイヤが付く軸) で考えます。ギア比 (M3508 は約 19.2、M2006 は 36) は、ライブラリが自動で計算します。
指示は、最後に呼んだものが有効です。

| 関数 | 単位 | 説明 |
|---|---|---|
| `robomas_set_rpm(m, rpm)` | 出力軸の rpm | 回転数を指定して回す。マイナスで逆回転 |
| `robomas_set_angle(m, 角度)` | 出力軸の角度 [度] | その角度まで回って止まる。720 なら 2 回転。電源を入れたときの位置が 0 度 |
| `robomas_set_current(m, 値)` | 電流の生の値 | M3508 は ±16384 (約 ±20A)、M2006 は ±10000 (約 ±10A) |
| `robomas_rpm(m)` | 出力軸の rpm | 今の回転数 |
| `robomas_angle(m)` | 出力軸の角度 [度] | 今の角度。何回転しても数え続ける |
| `robomas_reset_angle(m)` | — | 今の位置を 0 度にする (原点合わせ) |
| `robomas_current(m)` | 電流の生の値 | 今の電流 |
| `robomas_temp(m)` | ℃ | 今の温度 (M3508 だけ。M2006 は 0) |
| `robomas_ok(m)` | true / false | ESC からの返事が 100ms 以内に届いていれば true。届かないモーターは自動で止まる |

### 安全のために

- 初めて回すときは、タイヤを床から浮かせて、10rpm くらいの小さい値から試してください。
  `robomas_set_current_limit` で電流 (力) の上限も下げておくと、思ったより強く回ったときも安心です (下の「電流を制限する」)
- 1 本の CAN につなぐロボマスは **4 台まで**にしてください。1 台で CAN の約 13% を使い、指令のデータも 1ms ごとに送るので、
  4 台で約 65%、5 台だと約 91% が埋まってしまいます。それより多いときは CAN2 に分けます
- CAN の線が抜けたり ESC の電源が落ちたりしているあいだにモーターが回ると、その分は数えられません。
  そのあいだにローター (モーターの中の軸) が半回転より多く回ると、**ローター何回転ぶんでも角度がずれることがあります**。
  そういうことがあったら、`robomas_reset_angle` で原点を取り直してください
- `robomas_update` は、メインループからでも、タイマー割り込みの中からでも呼べます
- `robomas_update` を 1ms を待たずに何回も呼んでも、送るのは 1ms に 1 回だけです。
  毎回送ると、指令のデータが CAN を埋めて ESC からの返事が届かなくなり、全部のモーターが止まってしまうためです

### 加速をなめらかにする

`robomas_set_rpm` の目標を急に変えると、モーターは全力で加速します。タイヤが空回りしたり、ロボットがつんのめったり、
バッテリーの電圧が下がってマイコンがリセットしたりすることがあります。
`robomas_set_accel_limit` で「1 秒あたり何 rpm まで速さを変えてよいか」を決めると、目標に向かって少しずつ速さを変えます。

```c
/* USER CODE BEGIN 2 */
wheel = robomas_add(&rm, ROBOMAS_M3508, 1);
robomas_set_accel_limit(wheel, 600.0f);       // 1 秒で 600rpm まで (0 → 300rpm に 0.5 秒かけて上がる)
```

- 単位は出力軸の rpm/s です。初期値は 0 で、0 は「上限なし」(今までどおり、すぐ目標へ) です
- 効くのは `robomas_set_rpm` (速度制御) だけです。`robomas_set_angle`・`robomas_set_current` には効きません
- 回っている途中で `robomas_set_rpm` に切り替えたときは、今の回転数から少しずつ変えます (いったん 0 にはしません)
- `robomas_stop_all` には効きません。非常停止なので、すぐ止めます。ゆっくり止めたいときは `robomas_set_rpm(m, 0)` にします
- 「1ms ごとに `robomas_update`」を前提にしています (ゲインと同じ)

### 電流を制限する

`robomas_set_current_limit` で、モーターに流す電流の上限を決められます。電流は、モーターの力 (トルク) にほぼ比例します。
どの指示 (`robomas_set_rpm`・`robomas_set_angle`・`robomas_set_current`) でも、送る電流はこの値で止まります。

```c
/* USER CODE BEGIN 2 */
arm = robomas_add(&rm, ROBOMAS_M2006, 5);
robomas_set_current_limit(arm, 3000);         // M2006 を約 3A まで (アームを壁にぶつけても、力が出すぎない)
```

| 種類 | 設定できる範囲 (初期値は最大) | 1A あたり |
|---|---|---|
| M3508 | 0〜16384 (0〜約 20A) | 約 819 |
| M2006 | 0〜10000 (0〜約 10A) | 1000 |

- 初めて回すとき、機構がどこかに当たると壊れそうなとき、ぶつかっても大丈夫なように力を抑えたいときに使います
- 上限を下げすぎると、目標の速さまで上がらなかったり、重いものを持ち上げられなかったりします
- 範囲の外の値を書くと何も変わらず、`lib_error_hook` に `LIB_ERR_PARAM` が来ます

### PID のゲイン (効き具合) の調整

ライブラリは 2 段の PID 制御で、目標に合わせる電流を決めています。

- **速度 PID**：目標の回転数と今の回転数の差から、流す電流を決める (`robomas_set_rpm` のとき)
- **角度 PID**：目標の角度と今の角度の差から、目標の回転数を決めて、速度 PID に渡す (`robomas_set_angle` のとき)

それぞれの「強さ」をゲインと呼びます。P・I・D の 3 つがあり、役目が違います。

| ゲイン | 何を見るか | 上げるとどうなるか | 上げすぎると |
|---|---|---|---|
| **kp** (P：比例) | 今の差 | 目標に早く近づく | ブルブル振動する |
| **ki** (I：積分) | 差を足し続けた合計 | 最後に残る小さな差 (重さで少し届かない、など) が消える | ゆっくり大きく行き過ぎる |
| **kd** (D：微分) | 今の値の変化の速さ | 急な動きにブレーキがかかり、行き過ぎや振動がおさまる | 音が出る・細かく震える (値のガタつきを拾う) |

初期値は次のとおりです。**M2006 の速度以外は、実機で確かめていない仮の値**なので、調整してください。

| | 速度 kp / ki / kd | 速度の積分の上限 | 角度 kp / ki / kd | 角度のときの最高 rpm |
|---|---|---|---|---|
| M3508 | 1.5 / 0.08 / 0 (仮) | 5000 | 2.0 / 0 / 0 (仮) | 100 |
| M2006 | 1.5 / 0.08 / 0 (確認済み) | 5000 | 2.0 / 0 / 0 (仮) | 100 |

```c
/* USER CODE BEGIN 2 (robomas_add のあと) */
robomas_set_speed_pid(wheel, 1.5f, 0.08f, 0.0f);      // 速度：kp, ki, kd
robomas_set_angle_pid(arm, 2.0f, 0.0f, 0.0f);         // 角度：kp, ki, kd
```

速度の積分の上限と、角度制御のときの最高 rpm は、次の関数で変えます (ゲインは変わりません)。

```c
robomas_set_speed_i_limit(wheel, 5000.0f);           // 速度：積分の上限 (積分がたまりすぎないようにする)
robomas_set_angle_max_rpm(arm, 100.0f);              // 角度：目標の角度まで動くときの最高 rpm (出力軸)
```

**合わせる順番**：

1. **速度の kp**：`robomas_set_rpm` で目標を出し、`robomas_rpm` を printf で見ながら kp を少しずつ上げる。ブルブル振動したら下げる
2. **速度の ki**：目標にあと少し届かないときは、ki を少しずつ上げる
3. **速度の kd** (必要なときだけ)：行き過ぎたり、揺れが残ったりするときに、0 から少しずつ上げる。多くの場合は 0 のままで大丈夫です
4. **角度の kp**：`robomas_set_angle` で動かし、行き過ぎるなら角度の kp か最高 rpm を下げる
5. **角度の ki** (必要なときだけ)：アームが重さで少し下がったまま止まるなど、目標の角度に届かないときに、0 から少しずつ上げる
6. **角度の kd** (必要なときだけ)：止まるときに行き過ぎるなら、0 から少しずつ上げる

> [!NOTE]
> - ゲインは「1ms ごとに `robomas_update`」を前提にしています。呼ぶ間隔を変えると、効き方が変わります
> - D は、目標ではなく「今の値の変化」にかけています。スティックを急に倒して目標が急に変わっても、D が飛び出しません
> - 角度の I の分は、最高 rpm を超えないようにしています (たまりすぎて大きく行き過ぎるのを防ぐため)
> - ゲインはどれも 0 以上です。マイナスを書くと何も変わらず、`lib_error_hook` に `LIB_ERR_PARAM` が来ます

---

## 守ってほしいルール

### HAL の関数 (コールバック) を自分で書かない

講習 04 の `HAL_UARTEx_RxEventCallback` や 08 の `HAL_CAN_RxFifo0MsgPendingCallback` のように、
HAL が自動で呼ぶ関数は、1 つのプロジェクトに 1 つしか書けません。ライブラリは、使う部品の分を自分で持っています。
**同じ関数を自分でも書くと、ビルドで「multiple definition」というエラーになります。** そのときは自分で書いた方を消してください。

| 使う部品 | ライブラリが持っている関数 (自分では書かない) |
|---|---|
| SBUS (`sbus_init` を呼んだとき) | `HAL_UARTEx_RxEventCallback`、`HAL_UART_ErrorCallback` |
| CAN (`can_`・`robomas_`・`encoder_can_` のどれかを使ったとき) | `HAL_CAN_RxFifo0MsgPendingCallback`、`HAL_CAN_TxMailbox0/1/2CompleteCallback`、`HAL_CAN_TxMailbox0/1/2AbortCallback` |
| エンコーダー | なし (`HAL_TIM_PeriodElapsedCallback` は自由に使える) |

SBUS 以外の UART (Lidar など) の受信も使いたいときは、代わりに次の名前で書きます。ライブラリが使わない UART の分は、ここに回ってきます。

```c
void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size) { /* HAL_UARTEx_RxEventCallback の代わり */ }
void uart_error_hook(UART_HandleTypeDef *huart) { /* HAL_UART_ErrorCallback の代わり */ }
```

### 講習のコードから乗り換えるとき

講習で自分で書いたコードが残っているときは、次のようにします。

| 講習で書いたもの | ライブラリに乗り換えたら |
|---|---|
| `HAL_UARTEx_RxEventCallback`、`HAL_UART_ErrorCallback`、`HAL_CAN_RxFifo0MsgPendingCallback` | **消す** (残すと「multiple definition」のエラー) |
| CAN のフィルタの設定 (`HAL_CAN_ConfigFilter`) | **消す** (残すと、受け取れない ID が出る) |
| `HAL_CAN_Start`、`HAL_CAN_ActivateNotification`、`HAL_TIM_Encoder_Start`、`HAL_UARTEx_ReceiveToIdle_DMA` | 消すのがおすすめ。残っていても、`init` が止めてやり直すので動く |

### 自分の関数に同じ名前を付けない

ライブラリの関数の名前には頭の印がないので、自分の関数に `can_send` や `encoder_init` のような名前を付けると、
同じように「multiple definition」のエラーになります。そのときは自分の関数の名前を変えてください。

### init は 1 回、update は毎回

- `init` は `USER CODE BEGIN 2` で 1 回だけ。1 つの UART・CAN につき 1 回です
- `update` はループの中で毎回 (SBUS は毎回、エンコーダーは 10〜20ms ごと、ロボマスは 1ms ごと)

---

## うまくいかないとき

### 何がだめだったかを表示する

ライブラリは、失敗すると `lib_error_hook` という関数を呼びます。これを `main.c` の `USER CODE BEGIN 0` に書いておくと、
原因がシリアルモニターに出ます (講習 02 の `_write` も必要です)。

```c
/* USER CODE BEGIN 0 */
void lib_error_hook(lib_status_t err, const char *where) {   // ライブラリが失敗したら呼ばれる
  printf("lib error %d in %s\r\n", (int)err, where);        // 番号と、失敗した関数の名前を表示
}
```

| 番号 | 名前 | 意味 |
|---|---|---|
| 1 | `LIB_ERR_PARAM` | 渡した値がおかしい (ID が範囲外、同じ ID を 2 回登録、同じ CAN に `can_t` を 2 つ、など)。または呼ぶ順番が逆 (`can_init` より先に `can_listen`・`robomas_init`、`robomas_init` より先に `robomas_add`) |
| 2 | `LIB_ERR_FULL` | 登録の数か、送信の順番待ちがいっぱい (CAN の相手がいなくて送れない、など) |
| 3 | `LIB_ERR_HAL` | HAL の関数が失敗した (CubeMX の初期化 `MX_◯◯_Init()` より先に `init` を呼んだ、タイマーがエンコーダーモードになっていない、など) |
| 4 | `LIB_ERR_IRQ_OFF` | CubeMX で必要な割り込みや DMA が設定されていない (DMA が Circular の場合も) |

> [!NOTE]
> `lib_error_hook` は、CAN の線が抜けているときなどに 1ms ごとに呼ばれることがあり、割り込みの中から呼ばれることもあります。
> printf は時間がかかるので、動作を確かめるときだけ書いて、大会用のプログラムでは消してください。

### 症状ごとの対処

| 症状 | 原因と対処 |
|---|---|
| ビルドで `multiple definition of 'HAL_...'` | 自分で書いたコールバックとライブラリのものが重なっている。自分の方を消す (SBUS 以外の UART なら `uart_rx_event_hook` に移す) |
| ビルドで `multiple definition of 'can_send'` など | 自分の関数の名前がライブラリと同じ。自分の方の名前を変える |
| `dinosaur_library.h` が見つからない／`add_subdirectory` でエラー | CMakeLists.txt の 2 か所を足した？ ライブラリのフォルダはプロジェクトの隣で、名前はちょうど `dinosaur_library`？ (ZIP の「-main」が残っていない？) |
| `sbus_ok` がずっと 0 | 講習 04 のトラブルの表と同じ確認 (配線・反転・通信設定・NVIC)。DMA が Circular だと `sbus_init` が失敗する |
| スティックを離しても `sbus_stick` が 0 にならない／端まで倒しても ±1000 にならない | プロポの値がずれている。「[プロポの値がずれているとき](#プロポの値がずれているとき)」の手順で `sbus_set_channel_range` を書く |
| 値がずっと変わらない | ループで `sbus_update` / `encoder_update` を呼んでいる？ |
| `can_init` が `LIB_ERR_IRQ_OFF` | NVIC で「CAN1 RX0 interrupt」と「CAN1 TX interrupts」の両方にチェックした？ |
| CAN で何も受け取れない | `can_listen` より先に `can_init` を呼んだ？ (逆だと `lib error 1 in can_listen`) 講習 08 のフィルタの設定が残っていない？ 講習 08 のつまずきポイント (速度・終端抵抗・Normal モード) も確認 |
| `robomas_ok` がずっと false | ESC の ID と `robomas_add` の ID は同じ？ CAN は 1Mbps・Normal？ ESC に電源が入っている？ |
| `robomas_add` が NULL を返す | `robomas_init` より先に呼んでいない？ (`can_init` → `robomas_init` → `robomas_add` の順) ID は 1〜8？ 同じ ID を 2 回登録していない？ |

「つながっているはずなのに動かない」ときは、次の数を printf で見ると切り分けられます。

| 関数 | わかること |
|---|---|
| `sbus_frame_count(&sbus)` | SBUS のデータを何回受け取ったか。増えていなければ、配線か設定の問題 |
| `sbus_error_count(&sbus)` | UART のエラーが何回起きたか。どんどん増えるなら、反転や通信速度の設定ちがい |
| `can_dropped_count(&can1)` | 送れずに捨てたデータの数。増えるなら、相手がいないか、送りすぎ |
| `can_bus_age_ms(&can1)` | どの ID でもいいので、最後に何か届いてからの時間 [ms] |

---

## CAN の通信量の目安

1Mbps の CAN で、8 バイトのデータ 1 回分は約 0.13ms かかります。全部の基板で同じ線を使うので、送りすぎるとほかのデータが遅れます。

- **ロボマス**は 1 台ごとに 1ms ごとに返事を送ってくるので、1 台で約 13% を使います。
  4 台で約 65%、5 台で約 91% です (指令のデータも足して)。**1 本の CAN にはおよそ 4 台まで**にしてください
- **エンコーダー**を 6 個 1ms ごとに送ると約 8 割が埋まります。**10ms ごと (約 8%)** がおすすめです
- 自分で決める ID は、ロボマスが使う 0x1FF〜0x2FF とぶつからない **0x300 番台**がおすすめです

---

## 実機での確認

新しい基板やプロジェクトで初めて使うときは、次の順に確かめてください。

- [ ] **SBUS**：`sbus_raw` の値を表示し、スティックを動かすと変わる
- [ ] **SBUS**：受信機のコネクタを抜くと、0.2 秒以内に `sbus_ok` が 0 になる
- [ ] **SBUS**：送信機の電源を切ると `sbus_ok` が 0 になる
- [ ] **SBUS**：スティックを離すと `sbus_stick` が 0、端まで倒すと ±1000 になる (ならなければ `sbus_set_channel_range` で直す)
- [ ] **CAN**：2 枚の基板で `can_send` と `can_listen` をして、値が届く。片方の電源を切ると `can_age_ms` が増え続ける
- [ ] **エンコーダー**：手で 1 回転させると `encoder_turns` がだいたい 1.0 (マイナスなら `encoder_set_reverse`)
- [ ] **ロボマス**：タイヤを浮かせて、`robomas_set_rpm` に小さい値 (10rpm くらい) を入れると、その速さで回る
- [ ] **ロボマス**：`robomas_set_angle(m, 360)` で出力軸が 1 回転して止まる
- [ ] **ロボマス**：プロポを切ると止まる (`robomas_stop_all`)
- [ ] **ロボマス**：`robomas_set_accel_limit` を入れると、目標を急に変えても少しずつ速さが変わる
- [ ] **ロボマス**：`robomas_set_current_limit` を小さくすると、手で止めたときの力が弱くなる

---

## 関数の一覧

このライブラリを入れると使えるようになる関数の全部です。`include/` の中のファイルにも、同じことがコメントで書いてあります。
「場所」の列は、`main.c` のどこで呼ぶかの目安です (PV = `USER CODE BEGIN PV`、2 = `USER CODE BEGIN 2`、ループ = `while (1)` の中)。

### 入れ物 (型)

| 型 | 何の入れ物か | 作る数 |
|---|---|---|
| `sbus_t` | SBUS 1 つ分 | 受信機 1 つに 1 個 (2 個まで) |
| `encoder_t` | エンコーダー 1 個分 | エンコーダー 1 個に 1 個 |
| `can_t` | CAN 1 本分 | CAN 1 本に 1 個 (3 本まで) |
| `can_box_t` | CAN で届いたデータを 1 つの ID ぶんしまっておく箱 | 受け取る ID ごとに 1 個 |
| `encoder_can_rx_t` | CAN で届いたエンコーダーの値 | 受け取るエンコーダーごとに 1 個 |
| `robomas_t` | ロボマス全体 | CAN 1 本に 1 個 |
| `robomas_motor_t *` | ロボマス 1 台 (`robomas_add` が返す場所を入れる) | モーターごとに 1 個 |
| `lib_status_t` | 関数の結果。`LIB_OK` (0) なら成功、それ以外は「[何がだめだったかを表示する](#何がだめだったかを表示する)」の表の番号 | — |

### SBUS (`sbus_`)

| 関数 | 場所 | 返すもの | できること |
|---|---|---|---|
| `sbus_init(&sbus, &huart5)` | 2 | `lib_status_t` | UART の DMA 受信を始める。受信が止まっても自動で受信し直す |
| `sbus_update(&sbus)` | ループ | — | 届いた最新の値を写し取り、受信できているかを判定する。値を読む前に毎回呼ぶ |
| `sbus_ok(&sbus)` | ループ | true / false | 受信できていれば true。まだ届かない・0.2 秒届かない・送信機を見失ったときは false |
| `sbus_stick(&sbus, ch)` | ループ | −1000〜+1000 | スティックの値。真ん中で 0。`sbus_ok` が false なら 0 |
| `sbus_switch3(&sbus, ch)` | ループ | −1 / 0 / +1 | 3 段スイッチ。生の値が大きい方が +1。`sbus_ok` が false なら 0 |
| `sbus_switch2(&sbus, ch)` | ループ | 0 / 1 | 2 段スイッチ。`sbus_ok` が false なら 0 |
| `sbus_raw(&sbus, ch)` | ループ | 整数 (ch によって違う) | 届いた数字そのまま。スティックならだいたい 368〜1680、スイッチは段ごとに決まった値で、ch やプロポによって違う。`sbus_ok` が false でも最後に届いた値 |
| `sbus_set_stick_range(&sbus, 最小, 真ん中, 最大)` | 2 | — | 全部の ch の値の範囲を送信機に合わせる (初期値 368 / 1024 / 1680) |
| `sbus_set_deadband(&sbus, 幅)` | 2 | — | 全部の ch の不感帯 (初期値 26) |
| `sbus_set_channel_range(&sbus, ch, 最小, 真ん中, 最大)` | 2 | — | **その ch だけ**範囲を直す (値がずれたプロポ用) |
| `sbus_set_channel_deadband(&sbus, ch, 幅)` | 2 | — | **その ch だけ**不感帯を変える |
| `sbus_frame_count(&sbus)` | どこでも | 数 | 受け取ったデータの回数 (うまくいかないときの確認用) |
| `sbus_error_count(&sbus)` | どこでも | 数 | UART のエラーの回数 (うまくいかないときの確認用) |

`ch` は 0 から数えます (CH1 が `0`)。範囲の外の番号を渡すと 0 を返します。

### エンコーダー (`encoder_`)

| 関数 | 場所 | 返すもの | できること |
|---|---|---|---|
| `encoder_init(&enc, &htim4, 1 回転のカウント数)` | 2 | `lib_status_t` | タイマーで数え始め、今の位置を 0 にする。カウント数は PPR × 4 |
| `encoder_update(&enc)` | ループ (10〜20ms ごと) | — | タイマーを読んで、カウント・回転数を計算する |
| `encoder_rpm(&enc)` | ループ | 小数 | 回転数 [rpm]。逆回転はマイナス |
| `encoder_count(&enc)` | ループ | 整数 | 起動 (または `encoder_reset`) からのカウントの合計 |
| `encoder_delta(&enc)` | ループ | 整数 | 前の `encoder_update` からのカウントの差 |
| `encoder_turns(&enc)` | ループ | 小数 | 何回転したか (カウントの合計 ÷ 1 回転のカウント数) |
| `encoder_reset(&enc)` | どこでも | — | カウントの合計を 0 に戻す (原点合わせ) |
| `encoder_set_reverse(&enc, true)` | 2 | — | 数える向きを逆にする |

### CAN (`can_`)

| 関数 | 場所 | 返すもの | できること |
|---|---|---|---|
| `can_init(&can1, &hcan1)` | 2 | `lib_status_t` | フィルタの設定・開始・割り込みまで全部やる。ほかの `can_` より先に呼ぶ |
| `can_send(&can1, ID, d, バイト数)` | どこでも | `lib_status_t` | ID でデータを送る (0〜8 バイト)。すぐ送れなければ順番待ちして、積んだ順に送る |
| `can_listen(&can1, ID, &box)` | 2 | `lib_status_t` | その ID が届いたら `box` にしまっておく (最新の 1 回分) |
| `can_read(&box, r)` | ループ | true / false | 前に読んだあとに新しく届いていれば、`r` (8 バイト) に写して true |
| `can_peek(&box, r)` | ループ | true / false | 新しいかに関係なく、最後に届いたデータを `r` に写す。一度も届いていなければ false |
| `can_age_ms(&box)` | ループ | ms | その ID が最後に届いてからの時間。一度も届いていなければとても大きい数 (`UINT32_MAX`) |
| `can_bus_age_ms(&can1)` | ループ | ms | どの ID でもいいので、最後に何か届いてからの時間 |
| `can_dropped_count(&can1)` | どこでも | 数 | 送れずに捨てたデータの数 (うまくいかないときの確認用) |
| `can_subscribe(&can1, ID, 関数, NULL)` | 2 | `lib_status_t` | その ID が届いた瞬間に、自分の関数を呼んでもらう (割り込みの中で動くので短く書く) |
| `can_subscribe_range(&can1, 最初の ID, 最後の ID, 関数, NULL)` | 2 | `lib_status_t` | ID の範囲でまとめて、届いた瞬間に関数を呼んでもらう |

データの詰め方・取り出し方 (CAN の設定をしていないプロジェクトでも使えます)。`d` は 8 バイトの配列、`位置` は何バイト目から入れるか。

| 詰める | 取り出す | 値の範囲 | 使うバイト数 |
|---|---|---|---|
| `can_put_u8(d, 位置, 値)` | `can_get_u8(d, 位置)` | 0〜255 | 1 |
| `can_put_i8(d, 位置, 値)` | `can_get_i8(d, 位置)` | −128〜127 | 1 |
| `can_put_u16(d, 位置, 値)` | `can_get_u16(d, 位置)` | 0〜65535 | 2 |
| `can_put_i16(d, 位置, 値)` | `can_get_i16(d, 位置)` | −32768〜32767 | 2 |
| `can_put_u32(d, 位置, 値)` | `can_get_u32(d, 位置)` | 0〜約 42 億 | 4 |
| `can_put_i32(d, 位置, 値)` | `can_get_i32(d, 位置)` | 約 ±21 億 | 4 |
| `can_put_float(d, 位置, 値)` | `can_get_float(d, 位置)` | 小数 | 4 |

`can_subscribe` で呼んでもらう関数は、次の形で書きます。

```c
void on_rx(uint32_t id, const uint8_t *data, uint8_t len, void *arg) {
  // id：届いた ID、data：データ 8 バイト、len：データの長さ、arg：can_subscribe の最後に渡したもの
}
```

### エンコーダー CAN (`encoder_can_`)

| 関数 | 場所 | 返すもの | できること |
|---|---|---|---|
| `encoder_can_send(&can1, ID, &enc)` | ループ (10ms ごと) | `lib_status_t` | エンコーダーのカウントの合計と rpm を、1 回分のデータで送る |
| `encoder_can_listen(&can1, ID, &wheel)` | 2 | `lib_status_t` | 送られてくるエンコーダーの値を受け取り始める |
| `encoder_can_rpm(&wheel)` | ループ | 小数 | 送られてきた rpm。まだ届いていなければ 0 |
| `encoder_can_count(&wheel)` | ループ | 整数 | 送られてきたカウントの合計。まだ届いていなければ 0 |
| `encoder_can_age_ms(&wheel)` | ループ | ms | 最後に届いてからの時間。一度も届いていなければ `UINT32_MAX` |

### ロボマス (`robomas_`)

| 関数 | 場所 | 返すもの | できること |
|---|---|---|---|
| `robomas_init(&rm, &can1)` | 2 | `lib_status_t` | ESC からの返事を受け取り始める。`can_init` のあとに呼ぶ |
| `robomas_add(&rm, 種類, ESC の ID)` | 2 | モーターの場所 (失敗は NULL) | モーターを登録する。種類は `ROBOMAS_M3508` か `ROBOMAS_M2006`、ID は 1〜8 |
| `robomas_set_rpm(m, rpm)` | ループ | — | 出力軸の回転数を指定して回す |
| `robomas_set_angle(m, 角度)` | ループ | — | 出力軸の角度 [度] まで回って止まる |
| `robomas_set_current(m, 値)` | ループ | — | 電流を直接指定する (M3508 は ±16384、M2006 は ±10000 まで) |
| `robomas_update(&rm)` | ループ (1ms ごと) | `lib_status_t` | 全部のモーターの電流を計算して送る。同じ 1ms に何回呼んでも送るのは 1 回 |
| `robomas_stop_all(&rm)` | ループ | — | 全部のモーターを止める (電流 0)。送るのは次の `robomas_update` |
| `robomas_rpm(m)` | ループ | 小数 | 今の出力軸の回転数 [rpm] |
| `robomas_angle(m)` | ループ | 小数 | 今の出力軸の角度 [度]。何回転しても数え続ける |
| `robomas_reset_angle(m)` | どこでも | — | 今の位置を 0 度にする |
| `robomas_current(m)` | ループ | 整数 | 今の電流 (生の値) |
| `robomas_temp(m)` | ループ | ℃ | 今の温度 (M3508 だけ。M2006 は 0) |
| `robomas_ok(m)` | ループ | true / false | ESC からの返事が 100ms 以内に届いていれば true |
| `robomas_set_speed_pid(m, kp, ki, kd)` | 2 | — | 速度 PID のゲインを変える (初期値 1.5 / 0.08 / 0) |
| `robomas_set_angle_pid(m, kp, ki, kd)` | 2 | — | 角度 PID のゲインを変える (初期値 2.0 / 0 / 0) |
| `robomas_set_speed_i_limit(m, 上限)` | 2 | — | 速度の積分の上限を変える (初期値 5000) |
| `robomas_set_angle_max_rpm(m, rpm)` | 2 | — | 角度制御で目標まで動くときの最高回転数を変える (初期値 100) |
| `robomas_set_gear_ratio(m, ギア比)` | 2 | — | ギア比を変える (外付けのギアがあるときなど) |
| `robomas_set_accel_limit(m, rpm/s)` | 2 | — | 速度制御で、1 秒あたりに変えてよい速さの上限 (0 = 上限なし。初期値) |
| `robomas_set_current_limit(m, 値)` | 2 | — | 流す電流の上限 (初期値は M3508 が 16384、M2006 が 10000) |

### 自分で書くと呼ばれる関数

同じ名前で `main.c` に書くと、ライブラリが呼んでくれます。書かなければ何もしません。

| 関数 | いつ呼ばれるか |
|---|---|
| `void lib_error_hook(lib_status_t err, const char *where)` | ライブラリの関数が失敗したとき。`where` は失敗した関数の名前 |
| `void uart_rx_event_hook(UART_HandleTypeDef *huart, uint16_t size)` | SBUS 以外の UART で受信したとき (`HAL_UARTEx_RxEventCallback` の代わり) |
| `void uart_error_hook(UART_HandleTypeDef *huart)` | SBUS 以外の UART でエラーが起きたとき (`HAL_UART_ErrorCallback` の代わり) |

使い方の例は `examples/` フォルダにあります (プロポでロボマスを回す／エンコーダーを CAN で送る／受け取る)。

---

## 開発者向け

ライブラリ自体を直す人向けです。

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

設計の詳しいことは [`docs/specs/2026-10-20-dinosaur-library-design.md`](docs/specs/2026-10-20-dinosaur-library-design.md) にあります。
