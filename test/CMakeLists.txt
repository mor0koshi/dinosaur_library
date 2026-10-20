cmake_minimum_required(VERSION 3.22)
project(dinosaur_library_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# ライブラリのソースと HAL の偽物。テストの実行ファイルごとに全部まとめてリンクする
set(LIB_SOURCES
    ${LIB_DIR}/src/dinosaur_common.c
    ${LIB_DIR}/src/dinosaur_can.c
    ${LIB_DIR}/src/dinosaur_sbus.c
    ${LIB_DIR}/src/dinosaur_encoder.c
    ${LIB_DIR}/src/dinosaur_encoder_can.c
    ${LIB_DIR}/src/dinosaur_robomas.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_can.c
)

function(add_lib_test name)
    add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.c ${LIB_SOURCES})
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/fake
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LIB_DIR}/include
        ${LIB_DIR}/src
    )
    target_compile_options(${name} PRIVATE
        -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_options(${name} PRIVATE -fsanitize=address,undefined)
    target_link_libraries(${name} PRIVATE m)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_lib_test(test_port)
add_lib_test(test_can)
add_lib_test(test_sbus)
add_lib_test(test_encoder)
add_lib_test(test_encoder_can)
add_lib_test(test_robomas)
