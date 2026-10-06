# ============================================================================
# Android 原生构建（由 Gradle 通过 externalNativeBuild 调用）
#
# 产物：libmain.so —— 内含
#   core/            协议、加密、发现、多机管理（与桌面同一份源码）
#   ui/              drawUi() 界面 + 主题（同一份源码）
#   main_android.cpp SDL2 + GLES3 + ImGui 入口
#   Dear ImGui + imgui_impl_sdl2 + imgui_impl_opengl3
#   SDL2、libdatachannel、OpenSSL、nlohmann-json（外部依赖）
#
# 依赖获取方式（二选一，见 ../README.md）：
#   A) vcpkg 交叉编译： vcpkg install openssl libdatachannel nlohmann-json --triplet arm64-android
#      然后 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=arm64-android
#   B) 已有预编译目录： -DGO2_ANDROID_DEPS=<prefix>（含 include/ 与 lib/）
# ============================================================================
cmake_minimum_required(VERSION 3.18)
project(go2_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- 路径
# 本文件位于 client/apps/android/native/
get_filename_component(CLIENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
message(STATUS "[go2] client 目录: ${CLIENT_DIR}")

# SDL2 源码目录（setup.sh 会下载到 client/third_party/SDL）
set(SDL2_SOURCE_DIR "${CLIENT_DIR}/third_party/SDL" CACHE PATH "SDL2 源码目录")
if(NOT EXISTS "${SDL2_SOURCE_DIR}/include/SDL.h")
    message(FATAL_ERROR
        "找不到 SDL2 源码：${SDL2_SOURCE_DIR}\n"
        "请先在 client/apps/android/ 下执行 ./setup.sh（会自动下载 SDL2）")
endif()

# Dear ImGui 源码目录（桌面构建走 FetchContent，Android 由 setup.sh 复用同一个源码树或单独下载）
set(IMGUI_SOURCE_DIR "${CLIENT_DIR}/build/_deps/imgui-src" CACHE PATH "Dear ImGui 源码目录")
if(NOT EXISTS "${IMGUI_SOURCE_DIR}/imgui.cpp")
    set(IMGUI_SOURCE_DIR "${CLIENT_DIR}/third_party/imgui" CACHE PATH "Dear ImGui 源码目录" FORCE)
endif()
if(NOT EXISTS "${IMGUI_SOURCE_DIR}/imgui.cpp")
    message(FATAL_ERROR
        "找不到 Dear ImGui 源码：${IMGUI_SOURCE_DIR}\n"
        "请执行 ./setup.sh（会下载 imgui 到 client/third_party/imgui）")
endif()

# ---------------------------------------------------------------- 外部依赖
# A) vcpkg 工具链：find_package 直接可用
# B) GO2_ANDROID_DEPS 前缀目录：手工指定 include/lib
set(GO2_ANDROID_DEPS "" CACHE PATH "预编译依赖前缀目录（含 include/ 与 lib/）")
if(GO2_ANDROID_DEPS)
    list(APPEND CMAKE_PREFIX_PATH "${GO2_ANDROID_DEPS}")
    include_directories("${GO2_ANDROID_DEPS}/include")
    link_directories("${GO2_ANDROID_DEPS}/lib")
    # ★ NDK 工具链把 find_* 限制在 sysroot 内（CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY 等），
    #   而预编译前缀在 sysroot 之外 → 不放开的话 find_package(OpenSSL) 永远找不到
    #   （报 missing: OPENSSL_CRYPTO_LIBRARY）。放开并把路径钉死，最稳。
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
    set(OPENSSL_ROOT_DIR       "${GO2_ANDROID_DEPS}")
    set(OPENSSL_INCLUDE_DIR    "${GO2_ANDROID_DEPS}/include")
    set(OPENSSL_CRYPTO_LIBRARY "${GO2_ANDROID_DEPS}/lib/libcrypto.a")
    set(OPENSSL_SSL_LIBRARY    "${GO2_ANDROID_DEPS}/lib/libssl.a")
endif()

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
# nlohmann/json：优先包管理器；否则用 client/third_party 里的单头文件兜底
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND AND NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CLIENT_DIR}/third_party")
    message(STATUS "[go2] nlohmann/json: 用 ${CLIENT_DIR}/third_party 的单头文件")
endif()

# libdatachannel：优先用包管理器；找不到则退回手工前缀目录里的静态库
find_package(LibDataChannel CONFIG QUIET)
if(NOT LibDataChannel_FOUND AND GO2_ANDROID_DEPS)
    add_library(go2_rtc INTERFACE)
    target_include_directories(go2_rtc INTERFACE "${GO2_ANDROID_DEPS}/include")
    target_link_libraries(go2_rtc INTERFACE
        "${GO2_ANDROID_DEPS}/lib/libdatachannel.a"
        "${GO2_ANDROID_DEPS}/lib/libjuice.a"
        "${GO2_ANDROID_DEPS}/lib/libusrsctp.a")
endif()

# ---------------------------------------------------------------- SDL2
# SDL 的 Android 支持由源码树里的 CMake 提供（需要 -DSDL2 ANDROID 相关变量由 NDK 工具链提供）
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL2_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------- Dear ImGui
set(GO2_IMGUI_SOURCES
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
add_library(go2_imgui STATIC ${GO2_IMGUI_SOURCES})
target_include_directories(go2_imgui PUBLIC
    "${IMGUI_SOURCE_DIR}" "${IMGUI_SOURCE_DIR}/backends")
# ★ Android 上 ImGui 的 OpenGL3 后端必须走 ES3 分支（-DIMGUI_IMPL_OPENGL_ES3）：
#   否则它会被编成桌面 GL 加载器（imgl3w），链接期报一串
#   undefined symbol: glGetIntegerv / glBindBuffer / glCreateShader ...
target_compile_definitions(go2_imgui PUBLIC IMGUI_IMPL_OPENGL_ES3=1)
# GL / EGL 函数来自 Android 的 libGLESv3.so / libEGL.so；PUBLIC 会一路传给最终 main
target_link_libraries(go2_imgui PUBLIC SDL2::SDL2 GLESv3 EGL)

# ---------------------------------------------------------------- core（与桌面共享）
add_library(go2_core STATIC
    "${CLIENT_DIR}/core/crypto.cpp"
    "${CLIENT_DIR}/core/signaling.cpp"
    "${CLIENT_DIR}/core/protocol.cpp"         # 报文纯解析（回执；M2-6 回放基座）
    "${CLIENT_DIR}/core/robot_client.cpp"
    "${CLIENT_DIR}/core/robot_manager.cpp"
    "${CLIENT_DIR}/core/discovery.cpp"
    "${CLIENT_DIR}/core/local_keys.cpp"
    "${CLIENT_DIR}/core/webrtc_transport.cpp"  # ITransport 的 WebRTC 实现（M5 接缝）
    "${CLIENT_DIR}/core/sport_library.cpp"
    "${CLIENT_DIR}/core/unitree_cloud.cpp"
)
target_include_directories(go2_core PUBLIC
    "${CLIENT_DIR}/core" "${CLIENT_DIR}/platform")
target_link_libraries(go2_core PUBLIC
    OpenSSL::SSL OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
    android log)
if(TARGET LibDataChannel::LibDataChannel)
    target_link_libraries(go2_core PUBLIC LibDataChannel::LibDataChannel)
elseif(TARGET go2_rtc)
    target_link_libraries(go2_core PUBLIC go2_rtc)
endif()
# httplib（信令 HTTP 客户端，单头文件）
target_include_directories(go2_core PUBLIC "${CLIENT_DIR}/third_party")  # 若放 cpp-httplib 头
# 宇树云 API 走 HTTPS：httplib 必须在**编译 core 的那些 TU 时**就定义这个宏
# （桌面版只定义在 go2_remote 上，core 里的 https 实际是没开起来的 —— 这里一并修好）
target_compile_definitions(go2_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT=1)

# ---------------------------------------------------------------- ui（与桌面共享）
add_library(go2_ui STATIC
    "${CLIENT_DIR}/ui/ui.cpp"
    # ui.cpp 的拆分（M3-2）：共享声明在 ui/ui_internal.hpp，改这几个文件时保持同步
    "${CLIENT_DIR}/ui/ui_chrome.cpp"    # 顶栏 + 急停
    "${CLIENT_DIR}/ui/ui_actions.cpp"   # 动作库页（含动作→图标表）
    "${CLIENT_DIR}/ui/ui_remote.cpp"    # 遥控页
    "${CLIENT_DIR}/ui/ui_popups.cpp"    # 设备 / 设置 / 日志 三个弹窗
    "${CLIENT_DIR}/ui/ui_joystick.cpp"  # 悬浮双摇杆（绘制 + 命中区）
    "${CLIENT_DIR}/ui/ui_state.cpp"     # UiState 的纯数据实现（从 ui.cpp 抽出，见该文件头注释）
    "${CLIENT_DIR}/ui/command_service.cpp"  # 群控语义唯一实现（桌面/网页/安卓三端共用）
    "${CLIENT_DIR}/ui/layout.cpp"
    "${CLIENT_DIR}/ui/theme.cpp"
    "${CLIENT_DIR}/ui/web_bridge.cpp"   # 网页界面后端（本机 HTTP 服务 + /api/*），供安卓 WebView 使用
    "${CLIENT_DIR}/ui/key_probe.cpp"    # 从机器狗内网找钥匙（设置页 / 网页设置共用）
    "${CLIENT_DIR}/ui/textures.cpp"     # 狗卡片图片纹理（stb_image + GLES3 上传）
)
target_include_directories(go2_ui PUBLIC "${CLIENT_DIR}/ui")
target_link_libraries(go2_ui PUBLIC go2_core go2_imgui)

# ---------------------------------------------------------------- 网页资源清单（自动生成）
# 安卓端要把 APK assets/web 里的前端文件**导出到磁盘**（httplib 的静态目录只认磁盘路径），
# 而 APK 里的 assets 无法在运行时枚举 → 必须有一份"文件清单"。
#
# ★ 这份清单以前是 main_android.cpp 里手工维护的数组，漏加新文件就会：原生界面加载不到新图、
#   WebView 里 404（真实踩过：加 logo 时就差点漏掉 img/logo-word.png）。
#   现在改成**构建期用 GLOB 从 client/assets/web 真实目录生成**，新增文件自动进清单。
#   CONFIGURE_DEPENDS：新增/删除文件时 CMake 会自动重新配置，不用手动清缓存。
set(GO2_WEB_ASSET_DIR "${CLIENT_DIR}/assets/web")
file(GLOB_RECURSE GO2_WEB_FILES
    CONFIGURE_DEPENDS
    RELATIVE "${GO2_WEB_ASSET_DIR}"
    "${GO2_WEB_ASSET_DIR}/*")
# 清单只列"要给前端用的文件"：README 之类不必打进导出目录
list(FILTER GO2_WEB_FILES EXCLUDE REGEX "^README\\.md$")
list(SORT GO2_WEB_FILES)

list(LENGTH GO2_WEB_FILES GO2_WEB_COUNT)

set(GO2_WEB_LIST_HPP "${CMAKE_CURRENT_BINARY_DIR}/go2_web_files.hpp")
# ★ 刻意**不套 namespace**：这个头文件是在 main_android.cpp 的匿名命名空间里 include 的，
#   一旦里面写了 `namespace go2 { ... }`，就会变成 `(匿名)::go2`，
#   把后面所有 `go2::RobotManager` 之类的引用全遮蔽掉（编译报一串 no type named ...）。
file(WRITE "${GO2_WEB_LIST_HPP}"
    "// 本文件由 CMake 自动生成 —— 不要手改，改 client/assets/web 里的文件即可。\n"
    "// 生成源: apps/android/native/CMakeLists.txt（GLOB client/assets/web/**）\n"
    "// 注意: 故意不带 namespace，因为本文件是在匿名命名空间里被 include 的。\n"
    "#pragma once\n\n"
    "inline const char* const kWebFiles[] = {\n")
foreach(f ${GO2_WEB_FILES})
    file(APPEND "${GO2_WEB_LIST_HPP}" "    \"${f}\",\n")
endforeach()
file(APPEND "${GO2_WEB_LIST_HPP}"
    "};\ninline constexpr int kWebFileCount = ${GO2_WEB_COUNT};\n")
message(STATUS "[go2] 网页资源清单: ${GO2_WEB_COUNT} 个文件 → ${GO2_WEB_LIST_HPP}")

# ---------------------------------------------------------------- 最终产物 libmain.so
add_library(main SHARED main_android.cpp)
target_include_directories(main PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(main PRIVATE go2_ui go2_imgui SDL2::SDL2 SDL2::SDL2main)
target_compile_definitions(main PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT=1)
