#!/usr/bin/env bash
# 本物の CubeMX プロジェクトの設定 (HAL・コンパイルオプション) でライブラリがビルドできるか確かめる。
# 元のプロジェクトには触らず、一時フォルダにコピーしてビルドする。
#
# 使い方:
#   test/arm_build_check.sh <CubeMX プロジェクトのフォルダ> compile
#       ライブラリと例をコンパイルだけする (プロジェクト本体はリンクしない)
#   test/arm_build_check.sh <CubeMX プロジェクトのフォルダ> link <リンク確認用の .c> <期待するコールバック>...
#       .c を足してプロジェクト全体をリンクし、HAL のコールバックがライブラリのものになっているか確かめる。
#       <期待するコールバック>... はライブラリのものになっているべきコールバックの名前。
#       ここに書いたものが無い・HAL の空の関数のままなら NG。
#       ここに書いていないものでライブラリのものが見つかったら NG (使っていない部品が混ざっている)
set -euo pipefail

lib_dir=$(cd "$(dirname "$0")/.." && pwd)
src=$(cd "$1" && pwd)
mode=$2
name=$(basename "$src")
if [ "$mode" = link ] && [ $# -lt 4 ]; then
    echo "使い方: $0 <プロジェクト> link <リンク確認用の .c> <期待するコールバック>..." >&2
    exit 2
fi
expected=" ${*:4} "
work=$(mktemp -d "${TMPDIR:-/tmp}/dinosaur_check.XXXXXX")
proj="$work/$name"

cp -a "$src" "$proj"
rm -rf "$proj/build" "$proj/.git"

{
    echo ''
    echo '# ---- dinosaur_library のビルド確認 (arm_build_check.sh が追加) ----'
    echo 'set(DINOSAUR_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)'
    echo "add_subdirectory(\"$lib_dir\" dinosaur_library)"
    if [ "$mode" = link ]; then
        cp "$3" "$proj/linkcheck.c"
        echo 'target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/linkcheck.c)'
        echo 'target_link_libraries(${CMAKE_PROJECT_NAME} dinosaur_library)'
        # 呼ばれていない関数も残して、どのコールバックが選ばれたかを nm で見られるようにする
        echo 'target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--no-gc-sections)'
    fi
} >> "$proj/CMakeLists.txt"

cd "$proj"
cmake --preset Debug > /dev/null
if [ "$mode" = link ]; then
    cmake --build build/Debug 2>&1 | tee build.log
    cmake --build build/Debug --target dinosaur_examples 2>&1 | tee -a build.log
else
    cmake --build build/Debug --target dinosaur_library dinosaur_examples 2>&1 | tee build.log
fi

# ライブラリのフォルダ ($lib_dir) の中のファイルに出た警告だけを数える (フォルダ名を変えても動くように、実際のパスで見る)
if grep -F "$lib_dir/" build.log | grep -E ":[0-9]+:[0-9]+: warning"; then
    echo "NG: ライブラリか例に警告があります"
    exit 1
fi

if [ "$mode" = link ]; then
    elf=$(ls build/Debug/*.elf)
    for sym in HAL_CAN_RxFifo0MsgPendingCallback HAL_CAN_TxMailbox0CompleteCallback \
               HAL_UARTEx_RxEventCallback HAL_UART_ErrorCallback; do
        want=0
        case "$expected" in *" $sym "*) want=1 ;; esac
        line=$(arm-none-eabi-nm "$elf" | awk -v s="$sym" '$3 == s')
        if [ -z "$line" ]; then
            echo "$sym: (無し)"
            if [ "$want" = 1 ]; then
                echo "NG: $sym が無い (ライブラリのものがリンクされていない)"
                exit 1
            fi
            continue
        fi
        addr=$(echo "$line" | awk '{print $1}')
        type=$(echo "$line" | awk '{print $2}')
        where=$(arm-none-eabi-addr2line -e "$elf" "0x$addr")
        echo "$sym: $type $where"
        case "$where" in
            "$lib_dir"/src/*) in_lib=1 ;;
            *) in_lib=0 ;;
        esac
        if [ "$want" = 1 ] && [ "$in_lib" = 0 ]; then
            echo "NG: $sym が HAL の空の関数のまま (ライブラリのものになっていない)"
            exit 1
        fi
        if [ "$want" = 0 ] && [ "$in_lib" = 1 ]; then
            echo "NG: $sym (使っていない部品のコールバックがリンクされている)"
            exit 1
        fi
    done
fi

echo "OK: $name ($mode) $proj"
