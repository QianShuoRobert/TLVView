cmake_minimum_required(VERSION 3.10)

project(TLVView
    VERSION 1.0.0
    DESCRIPTION "轻量零拷贝 TLV (Tag-Length-Value) 编解码库"
    LANGUAGES CXX)

# TLVView 是 header-only 库（TLVView.h），无需编译为独立目标。
# 此处仅提供测试程序目标，便于验证库功能。

# C++ 标准：库本身兼容 C++11，测试程序使用 C++11。
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 测试程序：test.cpp
add_executable(tlvview_test test.cpp)

# MSVC 下启用 UTF-8 源码字符集，避免中文注释/字符串告警
if(MSVC)
    target_compile_options(tlvview_test PRIVATE /utf-8 /W3)
else()
    target_compile_options(tlvview_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 头文件搜索路径：当前目录
target_include_directories(tlvview_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 测试运行目标：构建后自动执行，输出测试结果
# CTest 需先 enable_testing()；此处用自定义 target 更直观
add_custom_target(run_test
    COMMAND $<TARGET_FILE:tlvview_test>
    DEPENDS tlvview_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "运行 TLVView 测试程序")
