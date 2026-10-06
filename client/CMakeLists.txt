cmake_minimum_required(VERSION 3.16)
project(go2_remote LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---------------------------------------------------------------- 依赖
find_package(OpenSSL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "系统未找到 glfw3，改用 FetchContent 构建")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)
endif()

# ---- cpp-httplib（单头文件 HTTP 客户端，用于 9991 信令） ----
FetchContent_Declare(cpphttplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.58.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(cpphttplib)

# ---- nlohmann/json ----
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ---- Dear ImGui ----
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL)

# ---- libdatachannel（WebRTC 数据通道） ----
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
set(USE_NICE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.24.5
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
    # Go2 兼容补丁：机器狗不回 DCEP ACK，需在收到首条数据时将通道置为已打开
    PATCH_COMMAND ${CMAKE_COMMAND} -DSRC_DIR=<SOURCE_DIR>
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/patches/apply_go2_fix.cmake)
FetchContent_MakeAvailable(libdatachannel)

# ---------------------------------------------------------------- 测试开关
enable_testing()

# ---------------------------------------------------------------- 编译告警
# 统一给**我们自己的**目标加基础告警（第三方依赖不动，免得它们的告警把日志刷爆）。
# 暂时不加 -Werror：先把存量告警清零，之后再收紧（见 docs/optimization_plan.md M2-1）。
function(go2_add_warnings tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4)
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ---------------------------------------------------------------- Sanitizer
# -DGO2_SANITIZE=address  → 查越界 / use-after-free（本项目历史上真出过悬垂引用崩溃）
# -DGO2_SANITIZE=thread   → 查数据竞争（UiState 跨线程共享那批字段就是它抓出来的思路）
# 只作用于本目录**之后**定义的目标，第三方依赖保持原样。
set(GO2_SANITIZE "" CACHE STRING "address | thread | 空 = 关闭")
if(GO2_SANITIZE)
    message(STATUS "Sanitizer 已开启: -fsanitize=${GO2_SANITIZE}")
    add_compile_options(-fsanitize=${GO2_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${GO2_SANITIZE})
endif()

# 正对照开关：给 ui_state_thread_test 注入一个**故意的**数据竞争，
# 用来证明"TSan 在本环境里确实抓得到竞争"。默认关闭 —— 只在你怀疑
# "0 报告是不是工具没生效" 时打开一次：
#   cmake -DGO2_RACE_POSITIVE_CONTROL=ON --preset linux-tsan && ctest -R ui_state_thread
option(GO2_RACE_POSITIVE_CONTROL "在并发测试里注入数据竞争（验证 TSan 有效）" OFF)

# ---------------------------------------------------------------- 目标
# ---- 分层（P0 跨平台重构）：core 纯逻辑 / platform 平台适配 / ui 界面 / apps 入口 ----
add_library(go2_core STATIC
    core/crypto.cpp
    core/signaling.cpp
    core/robot_client.cpp
    core/robot_manager.cpp
    core/discovery.cpp
    core/local_keys.cpp
    core/sport_library.cpp
    core/unitree_cloud.cpp
)
target_include_directories(go2_core PUBLIC core platform)
go2_add_warnings(go2_core)
target_link_libraries(go2_core PUBLIC
    LibDataChannel::LibDataChannel
    nlohmann_json::nlohmann_json
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)
if(TARGET httplib::httplib)
    target_link_libraries(go2_core PUBLIC httplib::httplib)
else()
    target_include_directories(go2_core PUBLIC ${cpphttplib_SOURCE_DIR})
endif()
# 宇树云 API 走 HTTPS：这个宏必须在**编译 core 里的 unitree_cloud.cpp 时**生效
# （只定义在 go2_remote 上时，core 内部的 https 实际没开起来 —— 顺手补上）
target_compile_definitions(go2_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT=1)

add_executable(go2_remote
    apps/desktop/main.cpp
    ui/theme.cpp
    ui/layout.cpp
    ui/ui.cpp
    ui/ui_state.cpp
    ui/command_service.cpp
    ui/web_bridge.cpp
    ui/key_probe.cpp
    ui/textures.cpp
)
target_include_directories(go2_remote PRIVATE ui third_party)
go2_add_warnings(go2_remote)
# 链上 core（其 PUBLIC include 目录会带出 core/ 与 platform/，源码里 #include "xxx.hpp" 无需改动）
target_link_libraries(go2_remote PRIVATE go2_core)

target_link_libraries(go2_remote PRIVATE
    imgui_lib
    LibDataChannel::LibDataChannel
    nlohmann_json::nlohmann_json
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)

if(TARGET httplib::httplib)
    target_link_libraries(go2_remote PRIVATE httplib::httplib)
else()
    target_include_directories(go2_remote PRIVATE ${cpphttplib_SOURCE_DIR})
endif()
# 开启 HTTPS（宇树云 API：登录 / 拉取每设备 AES-128 key）
target_compile_definitions(go2_remote PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT=1)

if(WIN32)
    target_compile_definitions(go2_remote PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    # Winsock + IP 助手（本机网段枚举 / 端口探测）
    target_link_libraries(go2_remote PRIVATE ws2_32 iphlpapi)
endif()

# ---------------------------------------------------------------- 无界面服务
# ---- go2_serve：只带 core + HTTP 服务，**不链 ImGui / GLFW / OpenGL** ----
# 为什么单独一个目标：Web 界面只是皮肤，核心控制逻辑（WebRTC / 信令 / 钥匙 /
# 指令表）都在 core/。桌面版把核心和 ImGui 编进了同一个可执行文件，导致
# "只想用网页控制"也得把整个图形程序跑起来。这个目标把它们拆开 ——
# 产物不带图形栈，服务器 / 容器 / CI 里都能跑。
add_executable(go2_serve
    apps/serve/main.cpp
    ui/ui_state.cpp
    ui/command_service.cpp
    ui/web_bridge.cpp
    ui/key_probe.cpp
)
target_include_directories(go2_serve PRIVATE ui)
go2_add_warnings(go2_serve)
target_link_libraries(go2_serve PRIVATE go2_core)
target_link_libraries(go2_serve PRIVATE
    LibDataChannel::LibDataChannel
    nlohmann_json::nlohmann_json
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)
if(TARGET httplib::httplib)
    target_link_libraries(go2_serve PRIVATE httplib::httplib)
else()
    target_include_directories(go2_serve PRIVATE ${cpphttplib_SOURCE_DIR})
endif()
target_compile_definitions(go2_serve PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT=1)
if(WIN32)
    target_compile_definitions(go2_serve PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(go2_serve PRIVATE ws2_32 iphlpapi)
endif()

# ---------------------------------------------------------------- 自测
#
# 约定：
#   · 纯逻辑 / 离线可跑的 → 注册进 ctest，`ctest --output-on-failure` 一把全过；
#   · 需要真机或会访问官方服务器的 → 照常构建，但标 DISABLED + LABELS manual，
#     不参与默认 ctest（否则在没有设备的机器上必然失败/挂住）。
# 每个测试都有 `go2_add_warnings`，测试代码自己也要保持零告警。

# ---- 加密 / 信令的自测（不需要机器狗在线）----
add_executable(crypto_test
    tests/crypto_test.cpp
    core/crypto.cpp
    core/signaling.cpp
)
target_include_directories(crypto_test PRIVATE core platform)
target_link_libraries(crypto_test PRIVATE
    nlohmann_json::nlohmann_json
    OpenSSL::SSL OpenSSL::Crypto
    Threads::Threads
)
if(TARGET httplib::httplib)
    target_link_libraries(crypto_test PRIVATE httplib::httplib)
else()
    target_include_directories(crypto_test PRIVATE ${cpphttplib_SOURCE_DIR})
endif()
go2_add_warnings(crypto_test)
add_test(NAME crypto_test COMMAND crypto_test)

# 双摇杆 / 急停决策的自测（纯逻辑，不需要机器狗与图形环境）
add_executable(motion_test tests/motion_test.cpp)
target_include_directories(motion_test PRIVATE core)
go2_add_warnings(motion_test)
add_test(NAME motion_test COMMAND motion_test)

# 断点布局的自测：makeLayout 是 constexpr 纯函数，各机型档位的预期值用 static_assert
# 钉死 —— 改了断点常量忘了同步预期，编译就会失败。
# ★ 必须把 ui/layout.cpp 一起编进来：这样它同时是**跨 TU 链接测试**。
#   曾经 makeLayout 声明成 constexpr 但定义在 .cpp，单文件语法检查全过，
#   直到链接才报 undefined symbol —— 只编译不链接是抓不到这类问题的。
add_executable(layout_test tests/layout_test.cpp ui/layout.cpp)
target_include_directories(layout_test PRIVATE ui)
go2_add_warnings(layout_test)
add_test(NAME layout_test COMMAND layout_test)

# ---- 运动指令表的自测（纯数据）----
add_executable(sport_library_test tests/sport_library_test.cpp core/sport_library.cpp)
target_include_directories(sport_library_test PRIVATE core)
target_link_libraries(sport_library_test PRIVATE nlohmann_json::nlohmann_json)
go2_add_warnings(sport_library_test)
add_test(NAME sport_library_test COMMAND sport_library_test)

# ---- 群控语义的自测（急停 / 锁定 / 目标集合 / 持续模式开关）----
# 用假 sink（tests/command_service_test.cpp 里的 FakeSink）驱动，不碰网络：
# 这条链路是安全动作的唯一实现，必须能离线回归。
add_executable(command_service_test
    tests/command_service_test.cpp
    ui/command_service.cpp
    ui/ui_state.cpp
)
target_include_directories(command_service_test PRIVATE ui)
target_link_libraries(command_service_test PRIVATE go2_core)
go2_add_warnings(command_service_test)
add_test(NAME command_service_test COMMAND command_service_test)

# ---- UiState 并发访问的自测 ----
# 界面线程与 Web 线程按真实用法反复冲同一份状态。
# 普通构建下保证"不崩"（并发访问 map/set 崩溃是最典型的失败模式）；
# 配上 -DGO2_SANITIZE=thread 则用来证明"0 data race"（见 docs/optimization_plan.md M2-2）。
add_executable(ui_state_thread_test
    tests/ui_state_thread_test.cpp
    ui/command_service.cpp
    ui/ui_state.cpp
)
target_include_directories(ui_state_thread_test PRIVATE ui)
target_link_libraries(ui_state_thread_test PRIVATE go2_core)
go2_add_warnings(ui_state_thread_test)
if(GO2_RACE_POSITIVE_CONTROL)
    target_compile_definitions(ui_state_thread_test PRIVATE GO2_RACE_POSITIVE_CONTROL=1)
endif()
add_test(NAME ui_state_thread_test COMMAND ui_state_thread_test)

# ---- 手动 / 联网自测：**不参与默认 ctest** ----
# discovery_test：直连 192.168.2.111 并扫描本机网段，必须真机在线才有意义；
# cloud_test    ：会访问宇树官方服务器 —— 项目纪律是"触达官方服务器的请求由用户本人执行"。
# 需要时显式运行：ctest -R discovery_test --output-on-failure
add_executable(discovery_test tests/discovery_test.cpp)
target_include_directories(discovery_test PRIVATE core platform)
target_link_libraries(discovery_test PRIVATE go2_core)
go2_add_warnings(discovery_test)
add_test(NAME discovery_test COMMAND discovery_test)
set_tests_properties(discovery_test PROPERTIES DISABLED TRUE LABELS manual)

add_executable(cloud_test tests/cloud_test.cpp)
target_include_directories(cloud_test PRIVATE core platform)
target_link_libraries(cloud_test PRIVATE go2_core)
go2_add_warnings(cloud_test)
add_test(NAME cloud_test COMMAND cloud_test)
set_tests_properties(cloud_test PROPERTIES DISABLED TRUE LABELS manual)
