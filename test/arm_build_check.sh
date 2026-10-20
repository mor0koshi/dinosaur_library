#!/usr/bin/env bash
# 本物の CubeMX プロジェクトの設定 (HAL・コンパイルオプション) でライブラリがビルドできるか確かめる。
# 元のプロジェクトには触らず、一時フォルダにコピーしてビルドする。
#
# 使い方:
#   test/arm_build_check.sh <CubeMX プロジェクトのフォルダ> compile
#       ライブラリと例をコンパイルだけする (プロジェクト本体はリンクしない)
#   test/arm_build_check.sh <CubeMX プロジェクトのフォルダ> link <リンク確認用の .c>
#       .c を足してプロジェクト全体をリンクし、HAL のコールバックがライブラリのものになっているか確かめる
set -euo pipefail

lib_dir=$(cd "$(dirname "$0")/.." && pwd)
src=$(cd "$1" && pwd)
mode=$2
name=$(basename "$src")
work=$(mktemp -d "${TMPDIR:-/tmp}/mor0koshi_check.XXXXXX")
proj="$work/$name"

cp -a "$src" "$proj"
rm -rf "$proj/build" "$proj/.git"

{
    echo ''
    echo '# ---- mor0koshi_library のビルド確認 (arm_build_check.sh が追加) ----'
    echo 'set(MOR0KOSHI_BUILD_EXAMPLES ON CACHE BOOL "" FORCE)'
    echo "add_subdirectory(\"$lib_dir\" mor0koshi_library)"
    if [ "$mode" = link ]; then
        cp "$3" "$proj/linkcheck.c"
        echo 'target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/linkcheck.c)'
        echo 'target_link_libraries(${CMAKE_PROJECT_NAME} mor0koshi_library)'
        # 呼ばれていない関数も残して、どのコールバックが選ばれたかを nm で見られるようにする
        echo 'target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--no-gc-sections)'
    fi
} >> "$proj/CMakeLists.txt"

cd "$proj"
cmake --preset Debug > /dev/null
if [ "$mode" = link ]; then
    cmake --build build/Debug 2>&1 | tee build.log
    cmake --build build/Debug --target mor0koshi_examples 2>&1 | tee -a build.log
else
    cmake --build build/Debug --target mor0koshi_library mor0koshi_examples 2>&1 | tee build.log
fi

if grep -E "mor0koshi_library/[^:]*:[0-9]+:[0-9]+: warning" build.log; then
    echo "NG: ライブラリか例に警告があります"
    exit 1
fi

if [ "$mode" = link ]; then
    elf=$(ls build/Debug/*.elf)
    for sym in HAL_CAN_RxFifo0MsgPendingCallback HAL_CAN_TxMailbox0CompleteCallback \
               HAL_UARTEx_RxEventCallback HAL_UART_ErrorCallback; do
        line=$(arm-none-eabi-nm "$elf" | awk -v s="$sym" '$3 == s')
        if [ -z "$line" ]; then
            echo "$sym: (無し)"
            continue
        fi
        addr=$(echo "$line" | awk '{print $1}')
        type=$(echo "$line" | awk '{print $2}')
        where=$(arm-none-eabi-addr2line -e "$elf" "0x$addr")
        echo "$sym: $type $where"
        case "$where" in
            *mor0koshi_library/src/*) ;;
            *) echo "NG: $sym が HAL の空の関数のまま (ライブラリのものになっていない)"; exit 1 ;;
        esac
    done
fi

echo "OK: $name ($mode) $proj"
