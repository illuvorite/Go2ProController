#pragma once

// ============================================================================
// Web UI 桥接：C++ 核心 ↔ 网页前端（Vue3）
//
// ★ 为什么走这条路：用户反馈 ImGui 界面"没质感"，而 CSS 想要渐变/阴影/模糊/过渡只是几行；
//   官方 App 本身就是 WebView + Web 前端（APK 里的 assets/dist 就是 Vue/Vite 产物），路子是验证过的。
//
// 架构（刻意做成"只换界面、不碰协议"）：
//   · 协议 / 加密 / 钥匙库 / 运动指令表全留在 C++（踩过无数坑，不重写）
//   · 这里只起一个**本机 HTTP 服务**：静态页面 + /api/state + /api/command
//   · 前端每 500ms 拉一次 state、点按钮就 POST 一条 command —— 没有视频，也就不用过媒体桥
//   · 之后想包成原生窗口（桌面 webview / 安卓 WebView）指向这个地址即可，前端一行不用改
//
// ⚠ 只监听 127.0.0.1：这是本机的控制界面，不对外暴露（也没有鉴权，别改成 0.0.0.0）
// ============================================================================

namespace go2 {

class RobotManager;
struct UiState;

/// 启动本机 UI 服务（后台线程）。默认 8123；**被占用会自动往后试 5 个端口**，
/// 实际用的端口会打印在 stdout（[WebUI] 界面已就绪 → http://127.0.0.1:xxxx）。
bool startWebUi(RobotManager& mgr, UiState& ui, int port = 8123);

/// 停止（进程退出前调，可省略）
void stopWebUi();

}  // namespace go2
