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
    ui/ui.cpp
)
target_include_directories(go2_remote PRIVATE ui)
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

# ---------------------------------------------------------------- 自测
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

# 双摇杆 / 急停决策的自测（纯逻辑，不需要机器狗与图形环境）
add_executable(motion_test tests/motion_test.cpp)
target_include_directories(motion_test PRIVATE core)
