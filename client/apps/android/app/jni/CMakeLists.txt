# 由 apps/android/setup.sh 生成：把 SDL 模板的 jni 目录接到我们的原生工程
# 必须是"合规顶层"：带 project() 且用 add_subdirectory（用 include 拼接过 → CMake 报 no project()）
cmake_minimum_required(VERSION 3.18)
project(go2_android_top LANGUAGES C CXX)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../native" "${CMAKE_BINARY_DIR}/go2native")
