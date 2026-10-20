cmake_minimum_required(VERSION 3.22)

# mor0koshi_library
#
# 使う側の CMakeLists.txt で:
#   add_subdirectory(../mor0koshi_library mor0koshi_library)
#   target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx mor0koshi_library)
#
# 静的ライブラリなので、呼んだ部品の .c (と、そこにある HAL のコールバック) だけがリンクされる。

add_library(mor0koshi_library STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_can.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_sbus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_encoder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mor0koshi_encoder_can.c
)
target_include_directories(mor0koshi_library
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# 使う側のプロジェクトの HAL のインクルードパスとマクロ (STM32F767xx など) でコンパイルする
target_link_libraries(mor0koshi_library PUBLIC stm32cubemx)
target_compile_options(mor0koshi_library PRIVATE -Wall -Wextra -Wdouble-promotion)
