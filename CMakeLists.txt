cmake_minimum_required(VERSION 3.22)

# dinosaur_library
#
# 使う側の CMakeLists.txt で:
#   add_subdirectory(../dinosaur_library dinosaur_library)
#   target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx dinosaur_library)
#
# 静的ライブラリなので、呼んだ部品の .c (と、そこにある HAL のコールバック) だけがリンクされる。

add_library(dinosaur_library STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dinosaur_common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dinosaur_can.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dinosaur_sbus.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dinosaur_encoder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dinosaur_encoder_can.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dinosaur_robomas.c
)
target_include_directories(dinosaur_library
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# 使う側のプロジェクトの HAL のインクルードパスとマクロ (STM32F767xx など) でコンパイルする
target_link_libraries(dinosaur_library PUBLIC stm32cubemx)
target_compile_options(dinosaur_library PRIVATE -Wall -Wextra -Wdouble-promotion)

# examples/ をコンパイルだけする (ビルド確認用。使う側のプロジェクトにはリンクしない)
option(DINOSAUR_BUILD_EXAMPLES "examples/ もコンパイルする (ビルド確認用)" OFF)
if(DINOSAUR_BUILD_EXAMPLES)
    add_library(dinosaur_examples OBJECT
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/robomas_sbus.c
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/encoder_sender.c
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/encoder_receiver.c
    )
    target_link_libraries(dinosaur_examples PRIVATE dinosaur_library)
    target_compile_options(dinosaur_examples PRIVATE -Wall -Wextra -Wdouble-promotion)
endif()
